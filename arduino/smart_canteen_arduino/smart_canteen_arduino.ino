/*
 * Smart Canteen Management System — Arduino Side
 *
 * Hardware connections (Arduino Uno / Nano):
 *   RFID (MFRC522) : SS=10, RST=9, MOSI=11, MISO=12, SCK=13
 *   LCD I2C 16×2   : SDA=A4, SCL=A5  (I2C address 0x27)
 *   4×4 Keypad     : Rows=4,5,6,7  Cols=A0,A1,A2,A3
 *   Buzzer         : Pin 8
 *   Green LED      : Pin 2
 *   Red LED        : Pin 3
 *   ESP01 TX→UNO 0 (RX)
 *   ESP01 RX←UNO 1 (TX)  (hardware Serial, 9600 baud)
 *
 * Libraries required (install via Arduino Library Manager):
 *   MFRC522        by GithubCommunity
 *   LiquidCrystal I2C  by Frank de Brabander
 *   Keypad         by Mark Stanley & Alexander Brevig
 */

#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>

// ── Pin definitions ─────────────────────────────────────────────────────────
#define RST_PIN    9
#define SS_PIN     10
#define BUZZER_PIN 8
#define GREEN_LED  2
#define RED_LED    3

// ── Peripherals ──────────────────────────────────────────────────────────────
MFRC522 rfid(SS_PIN, RST_PIN);
LiquidCrystal_I2C lcd(0x27, 16, 2);

const byte ROWS = 4, COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {4, 5, 6, 7};
byte colPins[COLS]  = {A0, A1, A2, A3};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ── Constants ────────────────────────────────────────────────────────────────
#define MAX_MENU  20
#define MAX_CART  10

// ── State machine ────────────────────────────────────────────────────────────
enum State { IDLE, WAIT_RESP, MENU_BROWSE, QTY_INPUT, CART_VIEW };
State state = IDLE;

// ── Data structures ──────────────────────────────────────────────────────────
struct UserInfo {
  char uid[20];
  char name[30];
  float balance;
};

struct MenuEntry {
  int   id;
  char  name[25];
  float price;
  int   quantity;
};

struct CartEntry {
  int   menuId;
  char  name[25];
  float price;
  int   qty;
};

UserInfo  currentUser;
MenuEntry menu[MAX_MENU];
int       menuCount = 0, menuIndex = 0;
CartEntry cart[MAX_CART];
int       cartCount = 0, cartIndex = 0;
String    qtyStr = "";

// ── Helpers ───────────────────────────────────────────────────────────────────
void lcdPrint(const char* l1, const char* l2 = nullptr) {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(l1);
  if (l2) { lcd.setCursor(0, 1); lcd.print(l2); }
}

void beepSuccess() {
  tone(BUZZER_PIN, 1000, 150); delay(200);
  tone(BUZZER_PIN, 1500, 150); delay(200);
  noTone(BUZZER_PIN);
}

void beepDeny() {
  tone(BUZZER_PIN, 400, 400); delay(450);
  tone(BUZZER_PIN, 300, 400); delay(450);
  noTone(BUZZER_PIN);
}

void beepConfirm() {
  tone(BUZZER_PIN, 1200, 80); delay(110);
  noTone(BUZZER_PIN);
}

void beepReady() {
  tone(BUZZER_PIN, 2000, 250); delay(280);
  tone(BUZZER_PIN, 2500, 250); delay(280);
  tone(BUZZER_PIN, 2000, 350); delay(400);
  noTone(BUZZER_PIN);
}

// ── Display helpers ────────────────────────────────────────────────────────────
void showMenuItem() {
  if (menuCount == 0) { lcdPrint("Menu is empty!", "D=Logout"); return; }
  char l1[17], l2[17];
  snprintf(l1, sizeof(l1), "%-11.11s%2d/%2d", menu[menuIndex].name, menuIndex + 1, menuCount);
  snprintf(l2, sizeof(l2), "Rs%-6.2f Qty:%-3d",  menu[menuIndex].price, menu[menuIndex].quantity);
  lcdPrint(l1, l2);
}

void showCartItem() {
  if (cartCount == 0) { lcdPrint("Cart is empty!", "D=Back"); return; }
  char l1[17], l2[17];
  snprintf(l1, sizeof(l1), "%d/%-2d %-11.11s", cartIndex + 1, cartCount, cart[cartIndex].name);
  float tot = cart[cartIndex].price * cart[cartIndex].qty;
  snprintf(l2, sizeof(l2), "%dx Rs%.2f=Rs%.1f", cart[cartIndex].qty, cart[cartIndex].price, tot);
  lcdPrint(l1, l2);
}

// ── Menu parser ────────────────────────────────────────────────────────────────
// Wire format from ESP01:  MENU:id,name,price,qty|id,name,price,qty|...
void parseMenu(const String& data) {
  menuCount = 0;
  int start = 0;
  while (start < (int)data.length() && menuCount < MAX_MENU) {
    int end = data.indexOf('|', start);
    if (end < 0) end = data.length();
    String tok = data.substring(start, end);
    int c1 = tok.indexOf(',');
    int c2 = tok.indexOf(',', c1 + 1);
    int c3 = tok.indexOf(',', c2 + 1);
    if (c1 > 0 && c2 > c1 && c3 > c2) {
      menu[menuCount].id       = tok.substring(0, c1).toInt();
      String nm = tok.substring(c1 + 1, c2);
      strncpy(menu[menuCount].name, nm.c_str(), sizeof(menu[0].name) - 1);
      menu[menuCount].name[sizeof(menu[0].name) - 1] = '\0';
      menu[menuCount].price    = tok.substring(c2 + 1, c3).toFloat();
      menu[menuCount].quantity = tok.substring(c3 + 1).toInt();
      menuCount++;
    }
    start = end + 1;
  }
}

// ── Reset to idle ──────────────────────────────────────────────────────────────
void resetSystem() {
  state      = IDLE;
  menuCount  = menuIndex = 0;
  cartCount  = cartIndex = 0;
  qtyStr     = "";
  memset(&currentUser, 0, sizeof(currentUser));
  lcdPrint("Scan Your Card", "----------------");
}

// ── Place order ────────────────────────────────────────────────────────────────
void placeOrder() {
  float total = 0;
  for (int i = 0; i < cartCount; i++) total += cart[i].price * cart[i].qty;
  char l2[17];
  snprintf(l2, sizeof(l2), "Total: Rs%.2f", total);
  lcdPrint("Placing order...", l2);
  state = WAIT_RESP;
  // ORDER:uid,total;menuId,qty;menuId,qty;...
  String msg = String("ORDER:") + currentUser.uid + "," + String(total, 2);
  for (int i = 0; i < cartCount; i++)
    msg += ";" + String(cart[i].menuId) + "," + String(cart[i].qty);
  Serial.println(msg);
}

// ── Handle message from ESP01 ─────────────────────────────────────────────────
void handleEspMsg(const String& msg) {

  if (msg.startsWith("GRANTED:")) {
    // GRANTED:name:balance
    String payload = msg.substring(8);
    int sep = payload.indexOf(':');
    String name = payload.substring(0, sep);
    float  bal  = payload.substring(sep + 1).toFloat();
    strncpy(currentUser.name, name.c_str(), sizeof(currentUser.name) - 1);
    currentUser.balance = bal;

    digitalWrite(GREEN_LED, HIGH);
    beepSuccess();
    delay(300);
    digitalWrite(GREEN_LED, LOW);

    char l1[17], l2[17];
    snprintf(l1, sizeof(l1), "Hi %-12.12s", currentUser.name);
    snprintf(l2, sizeof(l2), "Bal:Rs%.2f", currentUser.balance);
    lcdPrint(l1, l2);
    delay(1500);

    Serial.println("MENU:GET");
    lcdPrint("Loading menu...", "Please wait...");

  } else if (msg == "DENIED") {
    digitalWrite(RED_LED, HIGH);
    beepDeny();
    delay(300);
    digitalWrite(RED_LED, LOW);
    lcdPrint("Access Denied!", "Unknown Card");
    delay(2500);
    resetSystem();

  } else if (msg.startsWith("MENU:")) {
    parseMenu(msg.substring(5));
    menuIndex = 0;
    cartCount = 0;
    state = MENU_BROWSE;
    showMenuItem();

  } else if (msg.startsWith("ORDER_OK:")) {
    currentUser.balance = msg.substring(9).toFloat();
    char l2[17];
    snprintf(l2, sizeof(l2), "Bal:Rs%.2f", currentUser.balance);
    lcdPrint("Order Placed!", l2);
    beepSuccess(); delay(200); beepSuccess();
    delay(3000);
    resetSystem();

  } else if (msg == "ORDER_FAIL") {
    lcdPrint("Order Failed!", "Low Balance!");
    tone(BUZZER_PIN, 400, 800); delay(1000); noTone(BUZZER_PIN);
    state = MENU_BROWSE;
    showMenuItem();

  } else if (msg.startsWith("READY:")) {
    // Kitchen marked an order as ready
    String info = msg.substring(6);
    lcdPrint("Order Ready!", info.substring(0, 16).c_str());
    beepReady();
    delay(2500);
    // Restore previous display
    if      (state == MENU_BROWSE) showMenuItem();
    else if (state == CART_VIEW)   showCartItem();
    else                           lcdPrint("Scan Your Card", "----------------");
  }
}

// ── RFID scan ─────────────────────────────────────────────────────────────────
void checkRFID() {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return;
  String uid = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(rfid.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  strncpy(currentUser.uid, uid.c_str(), sizeof(currentUser.uid) - 1);
  lcdPrint("Verifying...", uid.substring(0, 16).c_str());
  state = WAIT_RESP;
  Serial.println("VERIFY:" + uid);
}

// ── Keypad handlers ───────────────────────────────────────────────────────────
void handleMenuKey() {
  char key = keypad.getKey();
  if (!key) return;
  switch (key) {
    case 'A': // next item
      if (menuCount) { menuIndex = (menuIndex + 1) % menuCount; showMenuItem(); }
      break;
    case 'B': // previous item
      if (menuCount) { menuIndex = (menuIndex - 1 + menuCount) % menuCount; showMenuItem(); }
      break;
    case 'C': // select item → quantity entry
      if (menuCount && menu[menuIndex].quantity > 0) {
        qtyStr = "";
        state  = QTY_INPUT;
        char l1[17];
        snprintf(l1, sizeof(l1), "%-16.16s", menu[menuIndex].name);
        lcdPrint(l1, "Qty: _");
      } else {
        lcdPrint("Out of stock!", ""); delay(1000); showMenuItem();
      }
      break;
    case '*': // place order
      if (cartCount == 0) { lcdPrint("Cart is empty!", "Add items first"); delay(1500); showMenuItem(); }
      else                  placeOrder();
      break;
    case '1': // view cart
      cartIndex = 0; state = CART_VIEW; showCartItem();
      break;
    case 'D': // logout
      lcdPrint("Logged out!", ""); delay(1200); resetSystem();
      break;
  }
}

void handleQtyKey() {
  char key = keypad.getKey();
  if (!key) return;
  if (key >= '0' && key <= '9') {
    if (qtyStr.length() < 3) { qtyStr += key; lcd.setCursor(5, 1); lcd.print(qtyStr + "   "); }
  } else if (key == '#') { // backspace
    if (qtyStr.length()) { qtyStr.remove(qtyStr.length() - 1); lcd.setCursor(5, 1); lcd.print(qtyStr + "   "); }
  } else if (key == 'C') { // confirm
    int qty = qtyStr.toInt();
    if (qty <= 0) {
      lcdPrint("Enter valid qty!", ""); delay(1000);
      char l1[17]; snprintf(l1, sizeof(l1), "%-16.16s", menu[menuIndex].name);
      lcdPrint(l1, "Qty: _"); qtyStr = "";
    } else if (qty > menu[menuIndex].quantity) {
      char msg[17]; snprintf(msg, sizeof(msg), "Max qty: %d", menu[menuIndex].quantity);
      lcdPrint("Not enough stock", msg); delay(1500);
      char l1[17]; snprintf(l1, sizeof(l1), "%-16.16s", menu[menuIndex].name);
      lcdPrint(l1, "Qty: _"); qtyStr = "";
    } else {
      // Merge into cart if same item already present
      bool found = false;
      for (int i = 0; i < cartCount; i++) {
        if (cart[i].menuId == menu[menuIndex].id) { cart[i].qty += qty; found = true; break; }
      }
      if (!found) {
        if (cartCount >= MAX_CART) { lcdPrint("Cart full!", "Max 10 items"); delay(1500); state = MENU_BROWSE; showMenuItem(); return; }
        CartEntry& c = cart[cartCount++];
        c.menuId = menu[menuIndex].id;
        strncpy(c.name, menu[menuIndex].name, sizeof(c.name) - 1);
        c.name[sizeof(c.name) - 1] = '\0';
        c.price = menu[menuIndex].price;
        c.qty   = qty;
      }
      beepConfirm();
      char l1[17]; snprintf(l1, sizeof(l1), "+%dx %-12.12s", qty, menu[menuIndex].name);
      lcdPrint(l1, "Added to cart!"); delay(1400);
      state = MENU_BROWSE; showMenuItem();
    }
  } else if (key == 'D') { // cancel
    state = MENU_BROWSE; showMenuItem();
  }
}

void handleCartKey() {
  char key = keypad.getKey();
  if (!key) return;
  switch (key) {
    case 'A': if (cartCount) { cartIndex = (cartIndex + 1) % cartCount; showCartItem(); } break;
    case 'B': if (cartCount) { cartIndex = (cartIndex - 1 + cartCount) % cartCount; showCartItem(); } break;
    case 'D': state = MENU_BROWSE; menuIndex = 0; showMenuItem(); break;
  }
}

// ── Setup & loop ──────────────────────────────────────────────────────────────
void setup() {
  // Hardware Serial communicates with ESP01 (disconnect ESP01 while uploading sketch)
  Serial.begin(9600);
  SPI.begin();
  rfid.PCD_Init();
  Wire.begin();
  lcd.init();
  lcd.backlight();

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(GREEN_LED,  OUTPUT);
  pinMode(RED_LED,    OUTPUT);

  // Startup screen
  lcd.clear();
  lcd.setCursor(1, 0); lcd.print("SMART CANTEEN");
  lcd.setCursor(0, 1); lcd.print("MANAGEMENT SYS");
  tone(BUZZER_PIN, 800, 150); delay(200);
  tone(BUZZER_PIN, 1000, 150); delay(200);
  tone(BUZZER_PIN, 1200, 250); delay(2200);
  noTone(BUZZER_PIN);

  lcdPrint("Scan Your Card", "----------------");
}

void loop() {
  // Incoming messages from ESP01
  if (Serial.available()) {
    String msg = Serial.readStringUntil('\n');
    msg.trim();
    if (msg.length()) handleEspMsg(msg);
  }

  switch (state) {
    case IDLE:        checkRFID();     break;
    case WAIT_RESP:                    break; // waiting for ESP01 reply
    case MENU_BROWSE: handleMenuKey(); break;
    case QTY_INPUT:   handleQtyKey();  break;
    case CART_VIEW:   handleCartKey(); break;
  }
}
