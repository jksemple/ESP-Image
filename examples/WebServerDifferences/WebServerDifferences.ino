#include "WiFi.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "Arduino.h"
#include <ESPAsyncWebServer.h>
#include <SD.h>
#include <FS.h>
#include "esp_image.h"

#include "wifi_secrets.h"
const char* ssid = SECRET_SSID;
const char* password = SECRET_PASSWORD;

// Correct for Xiao ESP32S3 Sense
#define SD_CARD_CS_PIN 21

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// Camera module pins XIAO ESP32S3 Sense
#define PWDN_GPIO_NUM  -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM  10
#define SIOD_GPIO_NUM  40
#define SIOC_GPIO_NUM  39

#define Y9_GPIO_NUM    48
#define Y8_GPIO_NUM    11
#define Y7_GPIO_NUM    12
#define Y6_GPIO_NUM    14
#define Y5_GPIO_NUM    16
#define Y4_GPIO_NUM    18
#define Y3_GPIO_NUM    17
#define Y2_GPIO_NUM    15
#define VSYNC_GPIO_NUM 38
#define HREF_GPIO_NUM  47
#define PCLK_GPIO_NUM  13

camera_fb_t* fb = NULL;
void capture() {

  // return buffer just before capture
  if (fb != NULL) {
    esp_camera_fb_return(fb);
  }
  log_i("Starting capture()");
  fb = esp_camera_fb_get();
  log_i("Done");
}
Image capturedImage;
Image rgbImage;
Image bmpImage;
Image prevRgbImage;
Image prevBmpImage;
Image diffImage;
Image diffBmpImage;
Image fileImage;

const char index_html[] PROGMEM = R"raw(
<!DOCTYPE HTML><html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { text-align:center; }
    .vert { margin-bottom: 10%; }
    .hori{ margin-bottom: 0%; }
  </style>
</head>
<body>
  <div id="container">
    <h2>Image library demo</h2>
    <p>
      <button onclick="buttonClick()">Take photo and process</button>
    </p>
    <p>Try making small changes between successive photos</p>
  </div>
  <div>
  <table width="100%">
  <tr><th>Captured image</th><th>Previous RGB 1/4 scale</th><th>RGB 1/4 scale</th><th>Difference</th><th>Previous difference from file</th></tr>
  <tr>
  <td width="20%"><img src="/showImage?image=captured" id="capturedImage" width="100%" ></td>
  <td width="20%"><img src="/showImage?image=rgbPrev" id="rgbImagePrev" width="100%"></td>
  <td width="20%"><img src="/showImage?image=rgb" id="rgbImage" width="100%"></td>
  <td width="20%"><img src="/showImage?image=diff" id="diffImage" width="100%"></td>
  <td width="20%"><img src="/showImage?image=file" id="fileImage" width="100%"></td>
  </tr>
  <tr>
  <td>JPG 240x240</td><td>BMP 60x60</td><td>BMP 60x60</td><td>BMP 240x240<br/><div id="response"></div></td><td>JPG 240x240</tr>
  </tr>
  </table>

  <script>
    function buttonClick() {
      var xhttp = new XMLHttpRequest();
      xhttp.open('GET', "/click", true);
      xhttp.onload = function() {
        //alert("onload");
        document.getElementById("capturedImage").src = "/showImage?image=captured#" + new Date().getTime();
        document.getElementById("rgbImage").src = "/showImage?image=rgb#" + new Date().getTime();
        document.getElementById("rgbImagePrev").src = "/showImage?image=rgbPrev#" + new Date().getTime();
        document.getElementById("diffImage").src = "/showImage?image=diff#" + new Date().getTime();
        document.getElementById("fileImage").src = "/showImage?image=file#" + new Date().getTime();
        document.getElementById("response").innerHTML = this.responseText;
      }
      xhttp.send();
    }
  </script>
</body>
</html>
)raw";

void setup() {
  
  //auto test = _malloc(1);
  
  Serial.begin(115200);

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }
  Serial.println("done.");
  while (!SD.begin(SD_CARD_CS_PIN)) {
    Serial.println("SD card not mounted");
    vTaskDelay(1000);
  }

  Serial.print("IP Address: http://");
  Serial.println(WiFi.localIP());

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  config.frame_size = FRAMESIZE_240X240;
  config.jpeg_quality = 10;
  config.fb_count = 1;
  config.grab_mode = CAMERA_GRAB_LATEST;
  
  esp_err_t err = esp_camera_init(&config);
  //auto sensor = esp_camera_sensor_get();
  //if (sensor->id.PID == OV3660_PID) {
  //  sensor->set_res_raw(sensor, 256, 0, 1823, 1547, 16, 6, 1788, 1564, 240, 240, true, false );
  //} 
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    while(1);
  }

  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send_P(200, "text/html", index_html);
  });

  server.on("/click", HTTP_GET, [](AsyncWebServerRequest * request) {

    Serial.println("Taking a photo...");
    capture();
    if (!fb) {
      Serial.println("Failed to capture");
      request->send_P(200, "text/plain", "failed");
      return;
    } else {
      log_i("Loading from camera");
      capturedImage.fromCamera(fb).load();
      //capturedImage.setTrueSize();
      log_i("Converting to RGB");
      if (rgbImage.hasContent()) prevRgbImage.fromImage(rgbImage).load();
      if (bmpImage.hasContent()) prevBmpImage.fromImage(bmpImage).load();

      // Convert captured image to RGB and scale down to 1/4 scale
      if (rgbImage.fromImage(capturedImage).convertTo(IMAGE_RGB565, SCALING_DIVIDE_4) != ESP_OK) {
        log_i("Error = %s", rgbImage.error.toCString());
        request->send(404);
        return;
      }
      // Convert scaled down RGB to BMP so it can be displayed
      if (bmpImage.fromImage(rgbImage).convertTo(IMAGE_BMP) != ESP_OK) {
        log_i("Error = %s", bmpImage.error.toCString());
        request->send(404);
        return;
      }
      if (diffImage.hasContent()) {
        fileImage.fromImage(diffImage).convertTo(IMAGE_JPEG);
        fileImage.toFile(SD, "/captured.bmp").save();
      }
      // Convert captured image to RGB without scaling so result of compareWith() can be displayed
      diffImage.fromImage(capturedImage).convertTo(IMAGE_RGB565);

      String response;
      int threshold = 50;
      if (rgbImage.hasContent() && prevRgbImage.hasContent()) {
        // Compare current RGB image with prevRGB image using a lambda expression
        // to define the comparison algorithm applied to each pair of compared pixels
        // This example lambda finds pixels that are significantly lighter than the corresponding
        // pixel in the previous image
        // The threshold value is passed into the lambda as a 'capture'
        float difference = rgbImage.compareWith(prevRgbImage, 1, [threshold] (int x, int y, Pixel thisPixel, Pixel prevPixel) {
          int prevGreyScale = prevPixel.grey();
          int newGreyScale = thisPixel.grey();
          
          if ((newGreyScale - prevGreyScale) > threshold) {
            // Plot a red pixel in diffImage where the comparison returns true
            diffImage.setPixel(4 * x, 4 * y, 255, 0, 0);
            return true;
          }
          return false;
        });
        log_i("Difference = %f", difference);
        response = String("Difference = ");
        response += String((int)(difference * 100));
        response += "%";
      }
      diffBmpImage.fromImage(diffImage).convertTo(IMAGE_BMP);

      // Printf style filename argument
      if (fileImage.fromFile(SD, "/%s.%s", "captured", "bmp").load() == ESP_OK) {
        log_i("Loaded ok");
      }
      log_i("Request complete");
      request->send_P(200, "text/plain", String(response).c_str());
    }
  });

  server.on("/showImage", HTTP_GET, [](AsyncWebServerRequest * request) {
    Serial.println("/showImage");
    String imageToShow;
    Image* pImage;
    if (request->hasParam("image")) {
      auto p = request->getParam("image", false);
      imageToShow = p->value();
    }
    if (imageToShow == "captured") {
      pImage = &capturedImage;
    } else 
    if (imageToShow == "rgb") {
      pImage = &bmpImage;
    } else 
    if (imageToShow == "rgbPrev") {
      pImage = &prevBmpImage;
    } else 
    if (imageToShow == "diff") {
      pImage = &diffBmpImage;
    } else 
    if (imageToShow == "file") {
      pImage = &fileImage;
    } 
            
    if (pImage->hasContent()) {
      AsyncWebServerResponse* response;
      if (pImage->type == IMAGE_JPEG) {
        log_i("JPG");
        response = request->beginResponse_P(200, "image/jpg", pImage->buffer, pImage->len);
      } else {
        log_i("BMP %d", pImage->len);
        response = request->beginResponse_P(200, "image/bmp", pImage->buffer, pImage->len);
      }
      response->addHeader("Cache-Control", "no-store, max-age=0, must-revalidate");
      request->send(response);
    } else {
      log_i("Empty");
      request->send(404);
    }
  });
  // Start server
  server.begin();

}

void loop() {
  
}

