[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)  [![Gitter](https://badges.gitter.im/cesanta/mongoose-os.svg)](https://gitter.im/cesanta/mongoose-os?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge)

# Twinkly library for Mongoose OS

[Twinkly Smart Decoration](http://twinkly.com) control library for Mongoose OS IoT framework

## Basic features implementation

* Turn ON / OFF
* Set brightness
* Custom device calls (using RPC)

## Communication

Communication performed using private REST API

### Comfig chema

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