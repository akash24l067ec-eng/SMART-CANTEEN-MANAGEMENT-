#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <SPI.h>
#include <MFRC522.h>

constexpr byte ROWS = 4;
constexpr byte COLS = 4;

char keys[ROWS][COLS] = {
  { '1', '2', '3', 'A' },
  { '4', '5', '6', 'B' },
  { '7', '8', '9', 'C' },
  { '*', '0', '#', 'D' }
};

byte rowPins[ROWS] = { A0, A1, A2, A3 };
byte colPins[COLS] = { 4, 5, 6, 7 };

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

LiquidCrystal_I2C lcd(0x27, 16, 2);

constexpr int RST_PIN = 9;
constexpr int SS_PIN = 10;
constexpr int LED_GRANTED = 2;
constexpr int LED_DENIED = 3;
constexpr int BUZZER_PIN = 8;

MFRC522 rfid(SS_PIN, RST_PIN);

struct MenuItem {
  int id;
  const char *name;
  int price;
  int quantity;
};

MenuItem menuItems[] = {
  { 1, "Veg Sandwich", 50, 20 },
  { 2, "Fruit Bowl", 40, 15 },
  { 3, "Veg Meals", 80, 10 }
};

constexpr int MENU_COUNT = sizeof(menuItems) / sizeof(menuItems[0]);

constexpr int MAX_CART_SIZE = 6;
constexpr int MAX_QTY_DIGITS = 2;
constexpr unsigned long FEEDBACK_DELAY_MS = 1200;

struct CartItem {
  int menuIndex;
  int quantity;
};

CartItem cart[MAX_CART_SIZE];
int cartCount = 0;

enum ScreenState { SHOW_TITLE, WAIT_CARD, MENU_BROWSE, QTY_SELECT, VIEW_CART };

ScreenState state = SHOW_TITLE;
unsigned long titleStart = 0;
int currentMenuIndex = 0;
int cartViewIndex = 0;
String currentUid = "";
String qtyInput = "";

void setup() {
  pinMode(LED_GRANTED, OUTPUT);
  pinMode(LED_DENIED, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  digitalWrite(LED_GRANTED, LOW);
  digitalWrite(LED_DENIED, LOW);

  lcd.init();
  lcd.backlight();

  Serial.begin(115200);
  SPI.begin();
  rfid.PCD_Init();

  showTitle();
}

void loop() {
  switch (state) {
    case SHOW_TITLE:
      if (millis() - titleStart > 2000) {
        enterWaitCard();
      }
      break;
    case WAIT_CARD:
      checkForCard();
      break;
    case MENU_BROWSE:
      handleMenuKeys();
      break;
    case QTY_SELECT:
      handleQtyKeys();
      break;
    case VIEW_CART:
      handleCartKeys();
      break;
  }
}

void showTitle() {
  state = SHOW_TITLE;
  titleStart = millis();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SMART CANTEEN");
  lcd.setCursor(0, 1);
  lcd.print("MANAGEMENT");
}

void enterWaitCard() {
  state = WAIT_CARD;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Scan your card");
  lcd.setCursor(0, 1);
  lcd.print("to continue");
}

void checkForCard() {
  if (!rfid.PICC_IsNewCardPresent()) {
    return;
  }
  if (!rfid.PICC_ReadCardSerial()) {
    return;
  }
  String uid = "";
  int reserveSize = rfid.uid.size > 0 ? rfid.uid.size * 3 - 1 : 0;
  // 2 hex chars + 1 colon separator per byte, minus 1 for no trailing colon.
  uid.reserve(reserveSize);
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (i > 0) {
      uid += ":";
    }
    if (rfid.uid.uidByte[i] < 0x10) {
      uid += "0";
    }
    uid += String(rfid.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();
  rfid.PICC_HaltA();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Verifying...");

  if (verifyUser(uid)) {
    currentUid = uid;
    flashAccess(true);
    currentMenuIndex = 0;
    showMenuItem();
    state = MENU_BROWSE;
  } else {
    flashAccess(false);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Access denied");
    delay(FEEDBACK_DELAY_MS);
    enterWaitCard();
  }
}

bool verifyUser(const String &uid) {
  Serial.print("VERIFY,");
  Serial.println(uid);
  String response = readSerialLine(3000);
  return response.startsWith("ACCESS");
}

void handleMenuKeys() {
  char key = keypad.getKey();
  if (!key) {
    return;
  }
  if (key == 'A') {
    currentMenuIndex = (currentMenuIndex - 1 + MENU_COUNT) % MENU_COUNT;
    showMenuItem();
  } else if (key == 'B') {
    currentMenuIndex = (currentMenuIndex + 1) % MENU_COUNT;
    showMenuItem();
  } else if (key == 'C') {
    qtyInput = "";
    state = QTY_SELECT;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Qty for:");
    lcd.setCursor(0, 1);
    lcd.print(menuItems[currentMenuIndex].name);
  } else if (key == '1') {
    if (cartCount > 0) {
      cartViewIndex = 0;
      state = VIEW_CART;
      showCartItem();
    }
  } else if (key == '*') {
    if (cartCount > 0) {
      placeOrder();
    }
  }
}

void handleQtyKeys() {
  char key = keypad.getKey();
  if (!key) {
    return;
  }
  if (key >= '0' && key <= '9') {
    if (qtyInput.length() < MAX_QTY_DIGITS) {
      qtyInput += key;
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Quantity:");
      lcd.setCursor(0, 1);
      lcd.print(qtyInput);
    }
  } else if (key == 'C') {
    int qty = qtyInput.toInt();
    if (qty > 0) {
      addToCart(currentMenuIndex, qty);
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Added to cart");
      delay(800);
      showMenuItem();
      state = MENU_BROWSE;
    }
  } else if (key == 'D') {
    showMenuItem();
    state = MENU_BROWSE;
  }
}

void handleCartKeys() {
  char key = keypad.getKey();
  if (!key) {
    return;
  }
  if (key == 'A') {
    cartViewIndex = (cartViewIndex - 1 + cartCount) % cartCount;
    showCartItem();
  } else if (key == 'B') {
    cartViewIndex = (cartViewIndex + 1) % cartCount;
    showCartItem();
  } else if (key == 'D') {
    showMenuItem();
    state = MENU_BROWSE;
  }
}

void showMenuItem() {
  const MenuItem &item = menuItems[currentMenuIndex];
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(item.name);
  lcd.setCursor(0, 1);
  lcd.print("Rs ");
  lcd.print(item.price);
  lcd.print(" Qty ");
  lcd.print(item.quantity);
}

void addToCart(int menuIndex, int quantity) {
  for (int i = 0; i < cartCount; i++) {
    if (cart[i].menuIndex == menuIndex) {
      cart[i].quantity += quantity;
      return;
    }
  }
  if (cartCount < MAX_CART_SIZE) {
    cart[cartCount] = { menuIndex, quantity };
    cartCount++;
  }
}

void showCartItem() {
  const CartItem &item = cart[cartViewIndex];
  const MenuItem &menu = menuItems[item.menuIndex];
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Cart item");
  lcd.setCursor(0, 1);
  lcd.print(menu.name);
  lcd.print(" x");
  lcd.print(item.quantity);
}

void placeOrder() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Placing order");

  Serial.print("ORDER,");
  Serial.print(currentUid);
  Serial.print(",");
  for (int i = 0; i < cartCount; i++) {
    Serial.print(menuItems[cart[i].menuIndex].id);
    Serial.print(":");
    Serial.print(cart[i].quantity);
    if (i < cartCount - 1) {
      Serial.print("|");
    }
  }
  Serial.println();

  String response = readSerialLine(4000);
  lcd.clear();
  if (response.startsWith("OK")) {
    lcd.setCursor(0, 0);
    lcd.print("Order placed!");
    cartCount = 0;
    delay(FEEDBACK_DELAY_MS);
    enterWaitCard();
  } else {
    lcd.setCursor(0, 0);
    lcd.print("Order failed");
    delay(FEEDBACK_DELAY_MS);
    showMenuItem();
    state = MENU_BROWSE;
  }
}

String readSerialLine(unsigned long timeoutMs) {
  unsigned long start = millis();
  String line = "";
  while (millis() - start < timeoutMs) {
    while (Serial.available() > 0) {
      char incoming = Serial.read();
      if (incoming == '\n') {
        line.trim();
        return line;
      }
      line += incoming;
    }
  }
  line.trim();
  return line;
}

void flashAccess(bool granted) {
  digitalWrite(granted ? LED_GRANTED : LED_DENIED, HIGH);
  tone(BUZZER_PIN, granted ? 1200 : 400, 200);
  delay(300);
  digitalWrite(granted ? LED_GRANTED : LED_DENIED, LOW);
}
