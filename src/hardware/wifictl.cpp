/****************************************************************************
 *   Tu May 22 21:23:51 2020
 *   Copyright  2020  Dirk Brosswick
 *   Email: dirk.brosswick@googlemail.com
 ****************************************************************************/
 
/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#include "config.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include "powermgm.h"
#include "wifictl.h"

#include "gui/statusbar.h"
#include "webserver/webserver.h"

bool wifi_init = false;
TaskHandle_t _WIFICTL_Task;

void wifictl_StartTask( void );
void wifictl_Task( void * pvParameters );
TaskHandle_t _wifictl_Task;

char *wifiname=NULL;
char *wifipassword=NULL;

struct networklist wifictl_networklist[ NETWORKLIST_ENTRYS ];

void wifictl_save_network( void );
void wifictl_load_network( void );
void wifictl_Task( void * pvParameters );

/*
 *
 */
void wifictl_setup( void ) {
    if ( wifi_init == true )
        return;
    wifi_init = true;
    powermgm_clear_event( POWERMGM_WIFI_ACTIVE | POWERMGM_WIFI_OFF_REQUEST | POWERMGM_WIFI_ON_REQUEST | POWERMGM_WIFI_CONNECTED | POWERMGM_WIFI_SCAN );

    // clean network list table
    for ( int entry = 0 ; entry < NETWORKLIST_ENTRYS ; entry++ ) {
      wifictl_networklist[ entry ].ssid[ 0 ] = '\0';
      wifictl_networklist[ entry ].password[ 0 ] = '\0';
    }

    // load network list from spiff
    wifictl_load_network();

    // register WiFi events
    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        powermgm_set_event( POWERMGM_WIFI_ACTIVE );
        powermgm_clear_event( POWERMGM_WIFI_OFF_REQUEST | POWERMGM_WIFI_ON_REQUEST | POWERMGM_WIFI_SCAN | POWERMGM_WIFI_CONNECTED );
        statusbar_style_icon( STATUSBAR_WIFI, STATUSBAR_STYLE_GRAY );
        statusbar_show_icon( STATUSBAR_WIFI );
        statusbar_wifi_set_state( true, "scan ..." );
        WiFi.scanNetworks();
    }, WiFiEvent_t::SYSTEM_EVENT_STA_DISCONNECTED);

    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        powermgm_set_event( POWERMGM_WIFI_ACTIVE );
        powermgm_clear_event( POWERMGM_WIFI_OFF_REQUEST | POWERMGM_WIFI_ON_REQUEST | POWERMGM_WIFI_SCAN | POWERMGM_WIFI_CONNECTED );
        statusbar_style_icon( STATUSBAR_WIFI, STATUSBAR_STYLE_GRAY );
        statusbar_show_icon( STATUSBAR_WIFI );
        int len = WiFi.scanComplete();
        for( int i = 0 ; i < len ; i++ ) {
          for ( int entry = 0 ; entry < NETWORKLIST_ENTRYS ; entry++ ) {
            if ( !strcmp( wifictl_networklist[ entry ].ssid,  WiFi.SSID(i).c_str() ) ) {
              wifiname = wifictl_networklist[ entry ].ssid;
              wifipassword = wifictl_networklist[ entry ].password;
              statusbar_wifi_set_state( true, "connecting ..." );
              WiFi.begin( wifiname, wifipassword );
              return;
            }
          }
        }
    }, WiFiEvent_t::SYSTEM_EVENT_SCAN_DONE );

    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        powermgm_set_event( POWERMGM_WIFI_CONNECTED | POWERMGM_WIFI_ACTIVE );
        powermgm_clear_event( POWERMGM_WIFI_OFF_REQUEST | POWERMGM_WIFI_ON_REQUEST | POWERMGM_WIFI_SCAN );
        statusbar_style_icon( STATUSBAR_WIFI, STATUSBAR_STYLE_WHITE );
        statusbar_show_icon( STATUSBAR_WIFI );
        String label(wifiname);
        label.concat(' ');
        label.concat(WiFi.localIP().toString());
        //If you want to see your IPv6 address too, uncomment this. 
        // label.concat('\n');
        // label.concat(WiFi.localIPv6().toString());
        statusbar_wifi_set_state( true, label.c_str() );
        asyncwebserver_setup();
    }, WiFiEvent_t::SYSTEM_EVENT_STA_GOT_IP );

    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        powermgm_set_event( POWERMGM_WIFI_ACTIVE | POWERMGM_WIFI_SCAN );
        powermgm_clear_event( POWERMGM_WIFI_CONNECTED | POWERMGM_WIFI_OFF_REQUEST | POWERMGM_WIFI_ON_REQUEST );
        statusbar_style_icon( STATUSBAR_WIFI, STATUSBAR_STYLE_GRAY );
        statusbar_show_icon( STATUSBAR_WIFI );
        statusbar_wifi_set_state( true, "scan ..." );
        WiFi.scanNetworks();
    }, WiFiEvent_t::SYSTEM_EVENT_WIFI_READY );

    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        powermgm_clear_event( POWERMGM_WIFI_ACTIVE | POWERMGM_WIFI_CONNECTED | POWERMGM_WIFI_OFF_REQUEST | POWERMGM_WIFI_ON_REQUEST | POWERMGM_WIFI_SCAN );
        statusbar_hide_icon( STATUSBAR_WIFI );
        statusbar_wifi_set_state( false, "" );
    }, WiFiEvent_t::SYSTEM_EVENT_STA_STOP );

  // start Wifo controll task
  xTaskCreate(  wifictl_Task,    /* Function to implement the task */
                "wifictl Task",       /* Name of the task */
                2000,                  /* Stack size in words */
                NULL,                   /* Task input parameter */
                1,                      /* Priority of the task */
                &_wifictl_Task );       /* Task handle. */ 
}

/*
 *
 */
void wifictl_save_network( void ) {
  fs::File file = SPIFFS.open( WIFICTL_CONFIG_FILE, FILE_WRITE );

  if ( !file ) {
    log_e("Can't save file: %s", WIFICTL_CONFIG_FILE );
  }
  else {
    file.write( (uint8_t *)wifictl_networklist, sizeof( wifictl_networklist  ) );
    file.close();
  }
}

/*
 *
 */
void wifictl_load_network( void ) {
  fs::File file = SPIFFS.open( WIFICTL_CONFIG_FILE, FILE_READ );

  if (!file) {
    log_e("Can't open file: %s", WIFICTL_CONFIG_FILE );
  }
  else {
    int filesize = file.size();
    if ( filesize > sizeof( wifictl_networklist  ) ) {
      log_e("Failed to read configfile. Wrong filesize!" );
    }
    else {
      file.read( (uint8_t *)wifictl_networklist, filesize );
    }
    file.close();
  }
}

/*
 *
 */
bool wifictl_is_known( const char* networkname ) {
  for( int entry = 0 ; entry < NETWORKLIST_ENTRYS; entry++ ) {
    if( !strcmp( networkname, wifictl_networklist[ entry ].ssid ) ) {
      return( true );
    }
  }
  return( false );
}

/*
 *
 */
bool wifictl_delete_network( const char *ssid ) {
  for( int entry = 0 ; entry < NETWORKLIST_ENTRYS; entry++ ) {
    if( !strcmp( ssid, wifictl_networklist[ entry ].ssid ) ) {
      wifictl_networklist[ entry ].ssid[ 0 ] = '\0';
      wifictl_networklist[ entry ].password[ 0 ] = '\0';
      wifictl_save_network();
      return( true );
    }
  }
  return( false );
}

/*
 *
 */
bool wifictl_insert_network( const char *ssid, const char *password ) {
  // check if existin
  for( int entry = 0 ; entry < NETWORKLIST_ENTRYS; entry++ ) {
    if( !strcmp( ssid, wifictl_networklist[ entry ].ssid ) ) {
      strncpy( wifictl_networklist[ entry ].ssid, ssid, sizeof( wifictl_networklist[ entry ].ssid ) );
      wifictl_save_network();
      WiFi.scanNetworks();
      powermgm_set_event( POWERMGM_WIFI_SCAN );
      return( true );
    }
  }
  // check for an emty entry
  for( int entry = 0 ; entry < NETWORKLIST_ENTRYS; entry++ ) {
    if( strlen( wifictl_networklist[ entry ].ssid ) == 0 ) {
      strncpy( wifictl_networklist[ entry ].ssid, ssid, sizeof( wifictl_networklist[ entry ].ssid ) );
      strncpy( wifictl_networklist[ entry ].password, password, sizeof( wifictl_networklist[ entry ].password ) );
      wifictl_save_network();
      WiFi.scanNetworks();
      powermgm_set_event( POWERMGM_WIFI_SCAN );
      return( true );
    }
  }
  return( false ); 
}

/*
 *
 */
void wifictl_on( void ) {
  if ( wifi_init == false )
    return;
    if ( powermgm_get_event( POWERMGM_WIFI_OFF_REQUEST ) || powermgm_get_event( POWERMGM_WIFI_ON_REQUEST )) {
      return;
    }
    else {
      powermgm_set_event( POWERMGM_WIFI_ON_REQUEST );
      vTaskResume( _wifictl_Task );
    }
}

/*
 *
 */
void wifictl_off( void ) {
  if ( wifi_init == false )
    return;
    if ( powermgm_get_event( POWERMGM_WIFI_OFF_REQUEST ) || powermgm_get_event( POWERMGM_WIFI_ON_REQUEST )) {
      return;
    }
    else {
      powermgm_set_event( POWERMGM_WIFI_OFF_REQUEST );
      vTaskResume( _wifictl_Task );
    }
}

/*
 * 
 */
void wifictl_Task( void * pvParameters ) {
  if ( wifi_init == false )
    return;

  while( true ) {
    vTaskDelay( 125 );
    if ( powermgm_get_event( POWERMGM_WIFI_ON_REQUEST ) ) {
      statusbar_wifi_set_state( true, "activate" );
      WiFi.mode( WIFI_STA );
      powermgm_clear_event( POWERMGM_WIFI_OFF_REQUEST | POWERMGM_WIFI_ACTIVE | POWERMGM_WIFI_CONNECTED | POWERMGM_WIFI_SCAN | POWERMGM_WIFI_ON_REQUEST );
    }
    else if ( powermgm_get_event( POWERMGM_WIFI_OFF_REQUEST ) ) {
      statusbar_wifi_set_state( false, "" );
      WiFi.mode( WIFI_OFF );
      esp_wifi_stop();
      powermgm_clear_event( POWERMGM_WIFI_OFF_REQUEST | POWERMGM_WIFI_ACTIVE | POWERMGM_WIFI_CONNECTED | POWERMGM_WIFI_SCAN | POWERMGM_WIFI_ON_REQUEST );
    }
    vTaskSuspend( _wifictl_Task );
  }
}