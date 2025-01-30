
/**
 * ESP-AI-SWITCH  ESP-AI 开关固件
 * 配网后将直接连接 ESP-AI 开放平台，用 ESP-AI 开发板可直接控制本开关。
 * ESP-AI 开发库的服务端配置中，指令配置可以指定 target_device_id 为本设备ID以控制本设备。
 *
 * 使用 esp-01s Relay 模块，或者自行使用 esp01s + 继电器。 
 * 继电器接 3.3或者5v都可以
 * -----------------
 * eps01s | 继电器
 *------------------
 * 0      |  NO（常开）
 * gnd    |  COM
*/

#include <ESP8266WiFi.h>
#include <Arduino.h>
#include <Arduino_JSON.h>
#include <WebSocketsClient.h>
#include <Hash.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>

// =================== 可修改部分 ==========================
// // 开放平台 api_key
// String api_key = "";
// // 设备名字
// String name = "AI点火器";
// // wifi 信息
// String ssid = "oldwang";
// String password = "oldwang520";
// // 打开开关后多少时间关闭，单位毫秒，0则不关闭
// // 如果是开关则设置为0， 如果是点火器请设置为 2000 等合适的时间
// int open_time = 2000;

// 开放平台 api_key
String api_key = "";
// 设备名字
String name = "AI点火器";
// wifi 信息
String ssid = "";
String password = "";
// 打开开关后多少时间关闭，单位毫秒，0则不关闭
// 如果是开关则设置为0， 如果是点火器请设置为 2000 等合适的时间
int open_time = 2000;
// ========================================================
// ========================================================

// 固件版本
String version = "0.0.1";
// 继电器引脚 高电平断开，低电平接通
int switch_pin = 0;
// 固件ID，这里写死
String bin_id = "100";
String inited = "";

WebSocketsClient webSocket;
ESP8266WebServer server(80);  // 建立WebServer，端口为80
bool is_scan_over = false;
bool is_scan_ing = false;
JSONVar esp_ai_wifi_scan_json_response_data;

//读取储存的wifi信息的结构体
struct SavedDatas {
  String inited;   // 是否已经初始化
  String ssid;     // ssid
  String pwd;      // pwd
  String name;     // 设备名字
  String api_key;  // 设备名字
  int open_time;   // 开关自动关闭延时
};

SavedDatas get_datas() {
  SavedDatas Info;

  int pos = 0;
  Info.inited = char(EEPROM.read(pos++));

  Info.ssid = readStringFromEEPROM(pos, 32);  // 读取 ssid
  pos += 32;

  Info.pwd = readStringFromEEPROM(pos, 32);  // 读取 pwd
  pos += 32;

  Info.name = readStringFromEEPROM(pos, 32);  // 读取 name
  pos += 32;

  Info.api_key = readStringFromEEPROM(pos, 32);  // 读取 api_key
  pos += 32;

  String open_time = readStringFromEEPROM(pos, 8);  // 读取 open_time 
  int num = atoi(open_time.c_str()); 
  Info.open_time = num;

  pos += 8;

  return Info;
}
String readStringFromEEPROM(int addr, int maxLen) {
  char data[maxLen + 1];
  for (int i = 0; i < maxLen; i++) {
    data[i] = EEPROM.read(addr + i);
  }
  data[maxLen] = '\0';  // 确保字符串以 '\0' 结尾
  return String(data);
}

void setup() {
  Serial.begin(115200);
  // Serial.setDebugOutput(true);
  Serial.println();
  Serial.println(); 

  pinMode(switch_pin, OUTPUT);
  digitalWrite(switch_pin, LOW);


  EEPROM.begin(512);
  SavedDatas datas = get_datas();
  inited = datas.inited;
  if (inited != "1") {
    EEPROM.put(0, "");
    EEPROM.commit();
  } else {
    ssid = datas.ssid;
    password = datas.pwd;
    name = datas.name;
    api_key = datas.api_key;
    open_time = datas.open_time;
    Serial.println("检测到有wifi信息，即将进行连接。");
    Serial.print("ssid：");
    Serial.print(ssid);
    Serial.print("  password：");
    Serial.print(password);
    Serial.print("  api_key：");
    Serial.print(api_key);
    Serial.print("  name：");
    Serial.print(name);
    Serial.print("  open_time：");
    Serial.print(open_time);

    Serial.println("");
    Serial.println("");


    Serial.println("如果您要清除wifi信息进行重新配网，请按下面步骤：");
    Serial.println("1. 打开开放平台 https://dev.espai.fun ");
    Serial.println("2. 打开超体页面，并且点击左上角 固件烧录 按钮。");
    Serial.println("3. 连接设备。");
    Serial.println("4. 点击 Erase Flash 按钮。");
    Serial.println("5. 重新烧录固件并配网。");
  }

  if (ssid == "") {
    // 打开配网页面
    Serial.println("请按下面步骤配网：");
    Serial.println("1. 用手机连接 ESP-AI-endpoint 热点");
    WiFi.mode(WIFI_AP);
    WiFi.softAP("ESP-AI-endpoint");
    IPAddress ip = WiFi.softAPIP();
    String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
    String httpUrl = "http://" + ipStr;
    Serial.print("2. 在浏览器打开下面地址：");
    Serial.println(httpUrl);
    Serial.println("3. 进行配网设置。");
    scan_wifi();
    initServer();
    return;
  }

  Serial.println();
  Serial.println();
  Serial.print("wifi 连接中 ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  int count = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    count += 1;
    if (count >= 20) {
      Serial.println("");
      Serial.println("连接超时，请重新配网。");

      Serial.println("请按下面步骤配网：");
      Serial.println("1. 用手机连接 ESP-AI-endpoint 热点");
      WiFi.mode(WIFI_AP);
      WiFi.softAP("ESP-AI-endpoint");
      IPAddress ip = WiFi.softAPIP();
      String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
      String httpUrl = "http://" + ipStr;
      Serial.print("2. 在浏览器打开下面地址：");
      Serial.println(httpUrl);
      Serial.println("3. 进行配网设置。");
      scan_wifi();
      initServer();
      break;
    }
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  String device_id = get_device_id();
  Serial.println("device_id: " + device_id);

  Serial.printf("Free Heap: %d KB\n", ESP.getFreeHeap() / 1024);

  String path = "/connect_espai_node?device_type=endpoint&device_id=" + device_id + "&version=" + version + "&api_key=" + api_key + "&name=" + urlEncode(name) + "&bin_id=" + bin_id + "&wifi_ssid=" + urlEncode(String(ssid));
  webSocket.beginSSL("api.espai.fun", 443, path.c_str()); 
  webSocket.onEvent(webSocketEvent_ye);
  webSocket.setReconnectInterval(3000);
  webSocket.enableHeartbeat(10000, 5000, 0);
}

// unsigned long messageTimestamp = 0;
void loop() {
  if(inited != "1"){
    // 处理客户端请求
    server.handleClient(); 
  }
  if(WiFi.status() != WL_CONNECTED){
    delay(100);
    return;
  }
  webSocket.loop();
}

// ========================= 业务服务的 ws 连接 =========================
void webSocketEvent_ye(WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      Serial.println("× ws服务已断开\n");
      break;
    case WStype_CONNECTED:
      {
        Serial.println("√ ws服务已连接\n");
        break;
      }
    case WStype_TEXT:
      {
        JSONVar parseRes = JSON.parse((char *)payload);
        if (JSON.typeof(parseRes) == "undefined") {
          return;
        }
        if (parseRes.hasOwnProperty("type")) {
          String type = (const char *)parseRes["type"];
          if (type == "open") {
            Serial.println("-> 收到 open 指令");
            digitalWrite(switch_pin, HIGH);
            if (open_time > 0) {
              Serial.println("-> 自动关闭");
              delay(open_time);
              digitalWrite(switch_pin, LOW);
            }
          } else if (type == "close") {
            Serial.println("-> 收到 close 指令");
            digitalWrite(switch_pin, LOW);
          } else if (type == "message") {
            String message = (const char *)parseRes["message"];
            Serial.print("-> 收到服务提示：");
            Serial.println(message);
          } else if (type == "clear") {
            Serial.println("收到清除程序指令");
            EEPROM.put(0, "");
            EEPROM.commit();
            delay(300);
            ESP.restart();
          }
        }
        break;
      }
    case WStype_ERROR:
      Serial.println("业务服务 WebSocket 连接错误");
      break;
  }
}

String get_device_id() {
  // 获取芯片 ID
  uint32_t chipId = ESP.getChipId();
  // 格式化为 MAC 地址风格
  char chipIdStr[18];
  snprintf(chipIdStr, sizeof(chipIdStr), "5C:CF:7F:%02X:%02X:%02X",
           (chipId >> 16) & 0xFF,
           (chipId >> 8) & 0xFF,
           chipId & 0xFF);
  return chipIdStr;
}

String urlEncode(String str) {
  String encoded = "";
  char c;
  char code0;
  char code1;
  for (uint16_t i = 0; i < str.length(); i++) {
    c = str[i];
    if (c == ' ') encoded += '+';
    else if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') encoded += c;
    else {
      code1 = (c & 0xf) + '0';
      if ((c & 0xf) > 9) code1 = (c & 0xf) - 10 + 'A';
      c = (c >> 4) & 0xf;
      code0 = c + '0';
      if (c > 9) code0 = c - 10 + 'A';
      encoded += '%';
      encoded += code0;
      encoded += code1;
    }
    yield();
  }
  return encoded;
}


// 初始化对外服务
void initServer() {
  /** 暴露业务服务接口 **/
  server.on("/", web_server_page_index);
  server.on("/set_config", set_config);
  server.on("/get_config", get_config);
  server.on("/get_ssids", get_ssids);
  server.begin();
}


void set_config() {
  String inited = "1";
  String ssid = server.arg("wifi_name");
  String pwd = server.arg("pwd");
  String api_key = server.arg("api_key");
  String name = server.arg("name");
  String open_time = String(server.arg("open_time"));

  Serial.println(("================== Set Config ===================="));
  Serial.println("设置 wifi_name：" + ssid);
  Serial.println("设置 pwd：" + pwd);
  Serial.println("设置 api_key：" + api_key);
  Serial.println("设置 name：" + name);
  Serial.println("设置 open_time：" + open_time);

  // 固定长度定义
  int pos = 0;
  EEPROM.write(pos++, inited[0]);                 // 单字符存储
  pos = writeStringToEEPROM(pos, ssid, 32);       // 写入 ssid
  pos = writeStringToEEPROM(pos, pwd, 32);        // 写入 pwd
  pos = writeStringToEEPROM(pos, name, 32);       // 写入 name
  pos = writeStringToEEPROM(pos, api_key, 32);    // 写入 api_key
  pos = writeStringToEEPROM(pos, open_time, 8);  // 写入 open_time
  EEPROM.commit();

  Serial.println(("==================================================="));
  Serial.println("即将重启开发板！");

  JSONVar json_response;
  JSONVar json_response_data;
  json_response["success"] = true;
  json_response["message"] = "配网成功，设备即将重启。";
  String send_data = JSON.stringify(json_response);

  setCrossOrigin();
  server.send(200, "application/json", send_data);

  delay(1000);
  ESP.restart();
}

int writeStringToEEPROM(int addr, const String &data, int maxLen) {
  int len = data.length();
  for (int i = 0; i < maxLen; i++) {
    if (i < len) {
      EEPROM.write(addr + i, data[i]);
    } else {
      EEPROM.write(addr + i, '\0');  // 用 '\0' 填充剩余部分
    }
  }
  return addr + maxLen;
}

void get_config() {
  SavedDatas datas = get_datas();
  String loc_device_id = get_device_id();

  JSONVar json_response;
  JSONVar json_response_data;
  json_response["success"] = true;
  json_response_data["device_id"] = loc_device_id;
  if (datas.inited == "1") {
    json_response_data["wifi_name"] = datas.ssid;
    json_response_data["wifi_pwd"] = datas.pwd;
    json_response_data["api_key"] = datas.api_key;
    json_response_data["open_time"] = datas.open_time;
    json_response_data["name"] = datas.name;
  }
  json_response["data"] = json_response_data;
  String send_data = JSON.stringify(json_response);

  setCrossOrigin();
  server.send(200, "application/json", send_data);
}


void scan_wifi() {
  Serial.println("开始扫描wifi");
  is_scan_over = false;
  is_scan_ing = true;
  int n = WiFi.scanNetworks();
  Serial.println("扫描wifi完毕");
  if (n == 0) {
    Serial.println("未搜索到任何 WIFI， 请重启开发板尝试。");
  } else {
    Serial.printf("\n\nWIFI扫描完成，共找到 %d 个网络\n", n);
    for (int i = 0; i < n; ++i) {
      // 中国 2.4ghz 信道 1-14
      JSONVar json_item;
      json_item["ssid"] = WiFi.SSID(i).c_str();
      json_item["rssi"] = WiFi.RSSI(i);
      json_item["channel"] = WiFi.channel(i);
      esp_ai_wifi_scan_json_response_data[i] = json_item;
      // Serial.printf("网络 %d: %s, 信号强度: %d dBm\n", i, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
    }
  }
  WiFi.scanDelete();
  is_scan_over = true;
  is_scan_ing = false;
}

void get_ssids() {
  JSONVar json_response;
  json_response["success"] = true;
  if (is_scan_ing == false && is_scan_over == false) {
    json_response["status"] = "scaning";
  } else {
    json_response["data"] = esp_ai_wifi_scan_json_response_data;
  }
  setCrossOrigin();
  String send_data = JSON.stringify(json_response); 
  server.send(200, "application/json", send_data);
}
void setCrossOrigin() {
  server.sendHeader(F("Access-Control-Allow-Origin"), F("*"));
  server.sendHeader(F("Access-Control-Max-Age"), F("600"));
  server.sendHeader(F("Access-Control-Allow-Methods"), F("PUT,POST,GET,OPTIONS"));
  server.sendHeader(F("Access-Control-Allow-Headers"), F("*"));
};

const char html_str[] PROGMEM = R"rawliteral(
    <!DOCTYPE html>
    <html lang='en'> 
    <head>
        <meta charset='UTF-8'>
        <meta name='viewport'
            content='viewport-fit=cover,width=device-width, initial-scale=1, maximum-scale=1, minimum-scale=1, user-scalable=no' />
        <title>ESP-AI 终端配网</title>
        <style>
            #ext4,
            #wakeup-type,
            #ssid {
                border-radius: 3px;
                font-size: 14px;
                padding: 12px 12px;
                box-sizing: border-box;
                border: 1px solid #cfcfcf;
                outline: none;
                width: 100%;
            }

            #loading {
                position: fixed;
                z-index: 2;
                top: 0px;
                bottom: 0px;
                left: 0px;
                right: 0px;
                background-color: rgba(51, 51, 51, 0.8);
                text-align: center;
                color: #fff;
                font-size: 22px;
                transition: 0.3s;
            }

            #loading .icon {
                border: 16px solid #f3f3f3;
                border-radius: 50%;
                border-top: 16px solid #09aba2;
                border-bottom: 16px solid #40cea5;
                width: 120px;
                height: 120px;
                margin: auto;
                position: absolute;
                bottom: 0px;
                top: 0px;
                left: 0px;
                right: 0px;
                -webkit-animation: spin 2s linear infinite;
                animation: spin 2s linear infinite;
            }

            @-webkit-keyframes spin {
                0% {
                    -webkit-transform: rotate(0deg);
                }

                100% {
                    -webkit-transform: rotate(360deg);
                }
            }

            @keyframes spin {
                0% {
                    transform: rotate(0deg);
                }

                100% {
                    transform: rotate(360deg);
                }
            }
        </style>
    </head>

    <body style='margin: 0; font-size: 16px; color: #333; position: relative;font-family: tnum;'>
        <div style='height: 100vh; width: 100vw; overflow-y: auto; overflow-x: hidden;padding: 24px 0px;box-sizing: border-box;'
            id='wifi_setting_panel'>
            <div style='padding-bottom: 18px;text-align: center;'>
                <img style='width:60px;' src='data:image/png;base64,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' />
                <div style='font-size: 22px;color: #757575;'>
                    为您的 ESP-AI-ENDPOINT 配网
                </div>
            </div>

            <div
                style='width: 100%;padding: 8px 24px;box-sizing: border-box;font-size: 14px;font-weight: 600;color: #757575;'>
                网络配置
            </div>

            <div style='width: 100%;padding: 8px 24px;box-sizing: border-box;'>
                <select id='ssid' style='background: #fff;padding-right: 20px;' placeholder='请选择 wifi'>
                    <option value='' id='scan-ing'>网络扫描中...</option>
                </select>
            </div>
            <div style='width: 100%;padding: 8px 24px;box-sizing: border-box;'>
                <input id='password' name='password' type='text' placeholder='请输入WIFI密码'
                    style='border-radius: 3px;font-size:14px;padding: 12px 12px;box-sizing: border-box;border: 1px solid #cfcfcf;outline: none;width:100%;'>
            </div>

          
            <div
                style='width: 100%;padding: 8px 24px;box-sizing: border-box;font-size: 14px;font-weight: 600;color: #757575;'>
                设备配置
            </div>
            <div style='width: 100%;padding: 8px 24px;box-sizing: border-box;' id='name_wrap'>
                <input id='name' name='name' type='text' placeholder='设备名字' value='AI点火器'
                    style='border-radius: 3px;font-size:14px;padding: 12px 12px;box-sizing: border-box;border: 1px solid #cfcfcf;outline: none;width:100%;'>
            </div>
            <div style='width: 100%;padding: 8px 24px;box-sizing: border-box;' id='open_time_wrap'>
                <input type='number' id='open_time' name='open_time' type='text' placeholder='开关打开后多少毫秒关闭'  value='2000'
                    style='border-radius: 3px;font-size:14px;padding: 12px 12px;box-sizing: border-box;border: 1px solid #cfcfcf;outline: none;width:100%;'>
            </div>
            <div style='color: red; font-size: 12px;padding: 0px 24px;'>如果是点火器建议设置为 2000 或者 3000 (1秒=1000毫秒)，如果不需要自动关闭请设置为 0 (普通开关形式)。</div>
            <div style='width: 100%;padding: 8px 24px;box-sizing: border-box;' id='api_key_wrap'>
                <input id='api_key' name='api_key' type='text' placeholder='开放平台秘钥'
                    style='border-radius: 3px;font-size:14px;padding: 12px 12px;box-sizing: border-box;border: 1px solid #cfcfcf;outline: none;width:100%;'>
            </div>

            <div style='width: 100%;text-align: right; padding: 6px 24px; padding-bottom: 32px; box-sizing: border-box;'>
                <button id='submit-btn'
                    style='width:100% ; box-sizing: border-box; border-radius: 3px; padding: 16px 0px;border: none;color: #fff;background-color: transparent; color:#fff; background: linear-gradient(45deg, #40cea5, #09aba2); box-shadow: 0px 0px 3px #ccc;letter-spacing: 2px;'>保存</button>
            </div>
            <div style='font-size: 13px;color:#929292;padding-top: 0px;text-align: center;'>
                设备编码：<span id='device_id'> - </span>
            </div>
            <div
                style='width:100%;padding-left: 24px;padding-top: 12px; font-size: 12px;color: #777;text-align: left;line-height: 22px;'>
                密钥获取方式：<br />
                1. 打开 ESP-AI 开放平台：<a href='https://dev.espai.fun' target='_block'>https://dev.espai.fun</a> <br />
                2. 创建超体并配置好各项服务 <br />
                3. 找到超体页面左下角 api key 内容，点击复制即可
            </div>
        </div>
        <div id='loading'>
            <div class='icon'></div>
        </div>
    </body>

    </html>

    <script>
        try {
            var domain = '';
            var scanTimer = null;

            var loading = false;
            function myFetch(apiName, params, cb) {
                fetch(domain + apiName, { mode: 'cors' })
                    .then(function (res) { return res.json() })
                    .then(function (data) {
                        cb && cb(data);
                    });
            };

            function get_config() {
                myFetch('/get_config', {}, function (res) {
                    console.log('wifi信息：', res);
                    document.querySelector('#loading').style.display = 'none';
                    if (res.success) {
                        var data = res.data;
                        if (data.wifi_name) {
                            document.querySelector('#ssid').value = data.wifi_name;
                        }
                        if (data.pwd) {
                            document.querySelector('#password').value = data.pwd;
                        }
                        if (data.api_key) {
                            document.querySelector('#api_key').value = data.api_key;
                        }
                        if (data.device_id) {
                            document.querySelector('#device_id').innerHTML = data.device_id;
                        }
                        if (data.open_time) {
                            document.querySelector('#open_time').innerHTML = data.open_time;
                        } 
                    } else {
                        alert('获取配置失败, 请刷新页面重试');
                    }
                });
            }

            function scan_ssids() {
                myFetch('/get_ssids', {}, function (res) {
                    if (res.success) { 
                        if (res.status === 'scaning' || !res.data) {
                            setTimeout(scan_ssids, 1000);
                            return;
                        }
                        var data = res.data || [];
                        if (data.length > 0) {
                            document.querySelector('#scan-ing').innerHTML = '请选择wifi...';
                        };
                        var selectDom = document.getElementById('ssid');
                        var options = selectDom.getElementsByTagName('option') || [];
                        var optionsName = [];
                        for (var i = 0; i < options.length; i++) {
                            optionsName.push(options[i].getAttribute('value'));
                        };
                        data.forEach(function (item) {
                            if (item.ssid && !optionsName.includes(item.ssid)) {
                                var option = document.createElement('option');
                                option.innerText = item.ssid + '     (' + (item.channel <= 14 ? '2.4GHz' : '5GHz') + ' 信道：' + item.channel + ')';
                                option.setAttribute('value', item.ssid);
                                selectDom.appendChild(option);
                            }
                        });
                        if (data.length) {
                            get_config();
                            clearInterval(scan_time);
                        };
                    } else {
                        alert('启动网络扫描失败，请刷新页面重试');
                    }
                });
            }

            function setWifiInfo() {
                if (loading) {
                    return;
                };
                var wifi_name = document.querySelector('#ssid').value;
                var wifi_pwd = document.querySelector('#password').value;
                var api_key = document.querySelector('#api_key').value;
                var name = document.querySelector('#name').value;
                var open_time = document.querySelector('#open_time').value; 
 
                if (!wifi_name) {
                    alert('请输入 WIFI 账号哦~');
                    return;
                }
                if (!wifi_pwd) {
                    alert('请输入 WIFI 密码哦~');
                    return;
                } 
                if (!name) {
                    alert('请输入设备名字哦~');
                    return;
                } 

                loading = true;
                document.querySelector('#submit-btn').innerHTML = '配网中...';
                clearTimeout(window.reloadTimer);
                window.reloadTimer = setTimeout(function () {
                    alert('未知配网状态，即将重启设备');
                    setTimeout(function () {
                        window.location.reload();
                    }, 2000);
                }, 20000);
                myFetch(
                    '/set_config?wifi_name=' + encodeURIComponent(wifi_name) +
                    '&pwd=' + encodeURIComponent(wifi_pwd) +
                    '&api_key=' + api_key +
                    '&name=' + encodeURIComponent(name) +
                    '&open_time=' + open_time,
                    {},
                    function (res) {
                        clearTimeout(window.reloadTimer);
                        loading = false;
                        document.querySelector('#submit-btn').innerHTML = '保存';

                        if (res.success) {
                            alert(res.message);
                            window.close();
                        } else {
                            alert(res.message);
                        }
                    }
                );
            };
  
            window.onload = function () {
                scan_ssids();
                document.querySelector('#submit-btn').addEventListener('click', setWifiInfo);
            }
        } catch (err) {
            alert('页面遇到了错误，请刷新重试：' + err);
        }

    </script>
  )rawliteral";

// 处理根目录请求，返回html页面
void web_server_page_index() {
  server.send(200, "text/html", html_str);
}
