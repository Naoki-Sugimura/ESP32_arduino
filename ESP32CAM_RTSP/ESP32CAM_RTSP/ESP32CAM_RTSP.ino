// https://github.com/geeksville/Micro-RTSP

#include <WiFi.h>
#include <WebServer.h>
#include <WiFiClient.h>
#include "OV2640.h"
#include "SimStreamer.h"
#include "OV2640Streamer.h"
#include "CRtspSession.h"

// --- Wi-FiとIPアドレスの設定 ---
char *ssid = "MOSAdemy";      // ご自身のSSIDに変更してください
char *password = "mosamosa"; // ご自身のパスワードに変更してください

// 固定したいIPアドレスの情報を設定
const IPAddress ip(192, 168, 137, 50);
const IPAddress gateway(192, 168, 137, 1);
const IPAddress subnet(255, 255, 255, 0);
const IPAddress dns1(192, 168, 137, 1); // DNSサーバーはゲートウェイと同じ場合が多い

// --- グローバル変数 ---
WebServer server(80);
WiFiServer rtspServer(554);
OV2640 cam;
CStreamer *streamer;

// mjpegストリームを処理
void handle_jpg_stream(void) {
  WiFiClient client = server.client();
  String response = "HTTP/1.1 200 OK\r\n";
  response += "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n";
  server.sendContent(response);

  while (1) {
    cam.run();
    if (!client.connected())
      break;
    response = "--frame\r\n";
    response += "Content-Type: image/jpeg\r\n\r\n";
    server.sendContent(response);

    client.write((char *)cam.getfb(), cam.getSize());
    server.sendContent("\r\n");
    if (!client.connected())
      break;
  }
}

// 静止画を処理
void handle_jpg(void) {
  WiFiClient client = server.client();
  cam.run();
  if (!client.connected()) {
    return;
  }
  String response = "HTTP/1.1 200 OK\r\n";
  response += "Content-disposition: inline; filename=capture.jpg\r\n";
  response += "Content-type: image/jpeg\r\n\r\n";
  server.sendContent(response);
  client.write((char *)cam.getfb(), cam.getSize());
}

// ページが見つからない場合のエラー処理
void handleNotFound() {
  String message = "Server is running!\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  server.send(200, "text/plain", message);
}

// --- ★★★ 修正箇所 ★★★ ---
// WiFiに接続する関数
void WifiConnect() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.println("Connecting to WiFi...");

  // ★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★
  // 1. WiFi.config() を呼び出して静的IPアドレスを設定します
  // ★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★★
  if (!WiFi.config(ip, gateway, subnet, dns1)) {
    Serial.println("STA Failed to configure");
  }

  // 2. WiFi.begin() で接続を開始します
  WiFi.begin(ssid, password);

  // 接続が完了するまで待機します
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connection successful");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP()); // 設定した固定IPアドレスが表示されます
}

void setup() {
  Serial.begin(115200);

  // カメラの設定
  // esp32cam_aithinker_config.frame_size = FRAMESIZE_SVGA;
  // VGA (640x480) または CIF (400x296) に変更してみる
  esp32cam_aithinker_config.frame_size = FRAMESIZE_VGA;
  esp32cam_aithinker_config.jpeg_quality = 10;
  cam.init(esp32cam_aithinker_config);

  // WiFiに接続
  WifiConnect();

  // サーバーのルーティング設定
  server.on("/", HTTP_GET, handle_jpg_stream);
  server.on("/jpg", HTTP_GET, handle_jpg);
  server.onNotFound(handleNotFound);

  // サーバーを開始
  server.begin();
  rtspServer.begin();
  streamer = new OV2640Streamer(cam); // ストリーミングサービスを開始

  Serial.println("WiFi connected");
  Serial.print("Use 'http://");
  Serial.print(WiFi.localIP());
  Serial.println("/' to watch mjpeg stream");
  Serial.print("Use 'http://");
  Serial.print(WiFi.localIP());
  Serial.println("/jpg' to see still jpg");
  Serial.print("Use RTSP: 'rtsp://");
  Serial.print(WiFi.localIP());
  Serial.println(":554/mjpeg/1' to start rtsp stream");
}

void loop() {
  // loop() 内で WifiConnect() を呼び出す必要はありません。
  // 接続が切れた場合の処理はより高度な実装が必要になりますが、
  // まずはsetup()での一回接続で動作させます。
  // WifiConnect(); // ← この行はコメントアウトまたは削除します

  server.handleClient();

  uint32_t msecPerFrame = 100;
  static uint32_t lastimage = millis();

  streamer->handleRequests(0);

  uint32_t now = millis();
  if (streamer->anySessions()) {
    if (now > lastimage + msecPerFrame || now < lastimage) {
      streamer->streamImage(now);
      lastimage = now;

      now = millis();
      if (now > lastimage + msecPerFrame) {
        printf("warning exceeding max frame rate of %d ms\n", now - lastimage);
      }
    }
  }

  WiFiClient rtspClient = rtspServer.accept();
  if (rtspClient) {
    Serial.print("client connected: ");
    Serial.println(rtspClient.remoteIP());
    streamer->addSession(rtspClient);
  }
}
