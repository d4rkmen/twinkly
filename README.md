[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)  [![Gitter](https://badges.gitter.im/cesanta/mongoose-os.svg)](https://gitter.im/cesanta/mongoose-os?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge)

# Twinkly library for Mongoose OS

[Twinkly Smart Decoration](http://twinkly.com) control library for Mongoose OS IoT framework

## Basic features implementation

* Turn ON / OFF
* Set brightness
* Custom device calls (using RPC)

## Communication

From the first Twinkly releases, the ARP was used to discover local network devices (Espressif MAC filtered). Current Twinkly devices are using UDP broadcast messages for discovery. THis is not implemented in library yet.

Twinkly device control performed using private REST API, but the latest firmware versions added MQTT support.
We can change MQTT broker host, port and user using the REST API. This way we don't need to poll device to read it's current state to detect changes happen. Just subscribe to correct topic and handle changes.
Unfortunatley, the newest devices (Gen2) use SSL connection to MQTT broker pors 8883, which makes impossible to use custom broker because or hardcoded CA inside the firmware. I wish the Twinkly developers consider to give user an option for CA cert and/or broker SSL enable/disable. 
I know @[sirioz](https://github.com/sirioz) taking serious user's feedback and have plans [to open the API](https://github.com/jghaanstra/com.twinkly/issues/5#issue-540867018) for everyone. So may be one day things will change.

### Config chema

```javascript
"twinkly": {
  "enable": true,         // Enable Twinkly library
  "rpc_enable": true,     // Enable RPC handlers
  "config_changed": true  // HAP configuration changed flag (internal use)
}
```

## RPC

* `Twinkly.List` - list stored devices
* `Twinkly.Add` `{ip:%Q}` - add new device
* `Twinkly.Remove` `{ip:%Q}` - remove stored device
* `Twinkly.Info` `{ip:%Q}` - show device info
* `Twinkly.Call` `{ip:%Q, method:%Q, data:%Q}` - call custom method

Example:

`$mos call --port ws://192.168.1.145/rpc twinkly.info '{"ip":"192.168.1.245"}' `

```javascript
{
  "product_name": "Twinkly",
  "hardware_version": "100",
  "bytes_per_led": 3,
  "hw_id": "xxxxxx",
  "flash_size": 64,
  "led_type": 19,
  "product_code": "TWS600STP",
  "fw_family": "M",
  "device_name": "Глобоза",
  "uptime": "90034823",
  "mac": "98:сс:сс:сс:сс:сс",
  "uuid": "СССССССС-СССС-СССС-СССС-СССССССССССС",
  "max_supported_led": 1200,
  "number_of_led": 600,
  "led_profile": "RGB",
  "frame_rate": 9,
  "measured_frame_rate": 10.1,
  "movie_capacity": 992,
  "wire_type": 1,
  "copyright": "LEDWORKS 2018",
  "code": 1000,
  "product": {
    "commercial_name": "Twinkly Strings 600",
    "device_family": null,
    "led_profile": "RGB",
    "led_number": 600,
    "default_name": "Strings",
    "layout_type": "2D",
    "pixel_shape": "dot",
    "mapping_allowed": true,
    "join_fml": "Strings",
    "sync_fml": "Strings",
    "bluetooth": true,
    "microphone": false,
    "icon": "string"
  }
}
```

`$mos call --port ws://192.168.1.145/rpc twinkly.call '{"ip":"192.168.1.246","method":"summary"}' `

```javascript
{
  "led_mode": {
    "mode": "off",
    "shop_mode": 0
  },
  "timer": {
    "time_now": 1555,
    "time_on": -1,
    "time_off": -1
  },
  "music": {
    "enabled": 1,
    "active": 0,
    "current_driverset": 0
  },
  "filters": [
    {
      "filter": "brightness",
      "config": {
        "value": 46,
        "mode": "enabled"
      }
    },
    {
      "filter": "hue",
      "config": {
        "value": 0,
        "mode": "disabled"
      }
    },
    {
      "filter": "saturation",
      "config": {
        "value": 0,
        "mode": "disabled"
      }
    }
  ],
  "group": {
    "mode": "none",
    "compat_mode": 0
  },
  "layout": {
    "uuid": "99349F0E-9EA9-E348-EE9A-BB8B644D4874"
  },
  "code": 1000
}
```

## Library usage

For a complete demonstration of library, look at this [Twinkly HomeKit Hub](https://github.com/d4rkmen/twinkly-homekit) project