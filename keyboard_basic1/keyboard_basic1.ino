#include <Arduino.h>
#include <SPI.h>
#include <ACAN2515.h>
#include <EspUsbHost.h>

// MCP2515引脚配置
static const byte MCP2515_CS = 10;                               // CS引脚
static const byte MCP2515_INT = 3;                               // 中断引脚
static const uint32_t QUARTZ_FREQUENCY = 8UL * 1000UL * 1000UL;  // 8MHz晶振

// ACAN2515实例
ACAN2515 can(MCP2515_CS, SPI, MCP2515_INT);

// 硬件定义
#define RGB_PIN 48
#define TS_HW_BUTTONBOX1_CATEGORY 27
#define CANBUS_BUTTONBOX_ADDRESS 0x711
#define CAN_TX 5
#define CAN_RX 4

// 全局变量
int gone = 1;
int color = 0;
unsigned long notWorking = 0;

// 发送CAN命令函数
void sendCMD(uint8_t modifier, uint8_t firstKey, uint8_t secondKey) {
  int retry = 5;
  bool sentSuccessfully = false;

  uint8_t payload[5];
  payload[0] = 0x5A;                       // 魔法字节
  payload[1] = 0;                          // 保留
  payload[2] = TS_HW_BUTTONBOX1_CATEGORY;  // 硬件按钮盒类别
  payload[3] = secondKey & 0xff;           // 数据
  payload[4] = firstKey & 0xff;            // 数据

  Serial.printf("Sending CAN frame: secondKey=%02X, firstKey=%02X\n", secondKey, firstKey);

  // 创建CAN消息
  CANMessage frame;
  frame.id = CANBUS_BUTTONBOX_ADDRESS;  // CAN总线按钮盒地址
  frame.ext = false;                    // 标准帧
  frame.rtr = false;                    // 数据帧
  frame.len = 5;                        // 数据长度
  memcpy(frame.data, payload, 5);       // 复制数据

  retry = 5;
  while (retry > 0) {
    if (can.tryToSend(frame)) {
      Serial.println("CAN frame sent successfully");
      sentSuccessfully = true;
      break;
    }
    Serial.println("Retry sending CAN frame...");
    delay(10);
    retry--;
  }

  if (!sentSuccessfully) {
    Serial.println("Failed to send CAN frame after retries");
    notWorking++;
  }
}


// USB主机类
class MyEspUsbHost : public EspUsbHost {
  void onGone(const usb_host_client_event_msg_t *eventMsg) {
    gone = 1;
    Serial.println("device gone");
  };

  void onReceive(const usb_transfer_t *transfer) {
    if (!transfer->data_buffer) return;
    int i = 0;
    int modifier = 0;
    int firstKey = 0;
    int secondKey = 0;

    for (i = 0; i < transfer->data_buffer_size && i < 50; i++) {
      Serial.printf("%02x ", transfer->data_buffer[i]);
    }
    Serial.println();

    if (transfer->num_bytes > 4 && transfer->data_buffer_size > 4) {
      modifier = (transfer->data_buffer[0]);
      firstKey = (transfer->data_buffer[2]);
      secondKey = (transfer->data_buffer[3]);

      if (firstKey > 0) firstKey += (modifier * 0xff);
      if (secondKey > 0) secondKey += (modifier * 0xff);

      if (firstKey > 0) {
        sendCMD(modifier, firstKey & 0xff, (firstKey >> 8) & 0xff);
      } else if (secondKey > 0) {
        sendCMD(modifier, secondKey & 0xff, (secondKey >> 8) & 0xff);
      }
    }
  }
};

MyEspUsbHost usbHost;

// CAN初始化函数
bool setupCAN() {
  Serial.println("Initializing MCP2515...");

  SPI.begin();

  // 配置CAN设置
  ACAN2515Settings settings(QUARTZ_FREQUENCY, 500UL * 1000UL);  // 500kbps
  settings.mRequestedMode = ACAN2515Settings::NormalMode;

  const uint16_t errorCode = can.begin(settings, [] {
    can.isr();
  });

  if (errorCode == 0) {
    Serial.println("CAN initialized successfully at 500kbps");
    return true;
  } else {
    Serial.print("CAN initialization error: 0x");
    Serial.println(errorCode, HEX);
    return false;
  }
}


// 处理接收到的CAN帧
void processCANReceive() {
  CANMessage rxFrame;
  while (can.receive(rxFrame)) {
    // 这里可以处理接收到的CAN帧
    // 例如：打印接收到的帧信息
    Serial.print("Received CAN frame ID: 0x");
    Serial.print(rxFrame.id, HEX);
    Serial.print(" Data: ");
    for (int i = 0; i < rxFrame.len; i++) {
      Serial.printf("%02X ", rxFrame.data[i]);
    }
    Serial.println();
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("ESP32-S3 Car Dashboard with MCP2515 Starting...");

  gone = 0;

  // 初始化USB主机
  usbHost.begin();
  usbHost.setHIDLocal(HID_LOCAL_Japan_Katakana);
  usbHost.task();

  // 初始化CAN
  if (!setupCAN()) {
    Serial.println("CAN initialization failed!");
  } else {
    Serial.println("CAN initialization successful");
  }

  // 设置RGB引脚
  pinMode(RGB_PIN, OUTPUT);
  digitalWrite(RGB_PIN, LOW);

  Serial.println("Setup complete");
}

void loop() {
  // 处理CAN接收
  processCANReceive();

  // 处理USB任务
  usbHost.task();

  // 短暂延迟以防止CPU占用过高
  delay(1);
}
