/*
 * エンジンパルスシミュレータ (スーパーカブ90用)
 */

#define VR_PIN A0
#define SW_PIN 2
#define PULSE_OUT_PIN 10 // ハードウェアPWM出力(OC1B)に固定

// 【パルス幅設定】マイクロ秒（us）単位
// 1カウント = 4マイクロ秒 (4μs)としているので4の倍数で設定する
#define PULSE_WIDTH_US 48

// 動作モード
enum Mode { MODE_MANUAL, MODE_SWEEP_PHASE1, MODE_SWEEP_PHASE2 };
Mode currentMode = MODE_MANUAL;

// ---------------------------------------------------------
// 【スイープ設定】(加減速の時間とRPMの範囲)
// ---------------------------------------------------------
unsigned long sweepStartTime = 0;
const unsigned long sweepDuration = 6000; 
const int sweepMinRPM = 2000;
const int sweepMaxRPM = 6000;

// ★スイープの開始方向を設定するフラグ
// true : Min(2500) → Max(4500) → Min(2500)
// false: Max(4500) → Min(2500) → Max(4500)
const bool startSweepFromMin = true; 

// ★追加：スイープを往復する回数を指定 (デフォルト1回)
const int sweepRepeatCount = 3; 

int currentSweepCycle = 0; // 現在の往復回数をカウントする変数
int lastAdcValue = -1;

// ---------------------------------------------------------
// シリアル表示用のグローバル変数
// ---------------------------------------------------------
int currentRPM = 0;                  // 現在のRPM保持用
unsigned long lastSerialTime = 0;    // 前回のシリアル送信時刻
const unsigned long serialInterval = 500; // 送信間隔 (500ms)

void setup() {
  // シリアル通信の初期化 (高速な115200bps推奨)
  Serial.begin(115200);
  while (!Serial) { ; } // シリアルポートの準備待ち
  Serial.println(F("========================================"));
  Serial.println(F(" カブ90用 高精度パルスシミュレータ 始動 "));
  Serial.println(F("========================================"));

  pinMode(SW_PIN, INPUT_PULLUP);
  pinMode(PULSE_OUT_PIN, OUTPUT);
  digitalWrite(PULSE_OUT_PIN, LOW); // 普段はLOW

  // Timer1 ハードウェア設定 (Active-HIGH)
  TCCR1A = _BV(COM1B1) | _BV(WGM11) | _BV(WGM10);
  TCCR1B = _BV(WGM13) | _BV(WGM12) | _BV(CS11) | _BV(CS10);

  // パルス幅をレジスタに設定 (1カウント = 4us)
  OCR1B = PULSE_WIDTH_US / 4;

  // 初期回転数をセット (1000 RPM)
  setRPM(1000);
}

void loop() {
  // モード切替（スイッチ押下判定）
  if (currentMode == MODE_MANUAL && digitalRead(SW_PIN) == LOW) {
    delay(30); // 簡易デバウンス
    if (digitalRead(SW_PIN) == LOW) {
      currentMode = MODE_SWEEP_PHASE1;
      sweepStartTime = millis();
      currentSweepCycle = 1; // 1回目の往復を開始
      
      Serial.print(F("\n[INFO] オートスイープ開始 ["));
      Serial.print(currentSweepCycle);
      Serial.print(F("/"));
      Serial.print(sweepRepeatCount);
      Serial.print(F("回目] "));
      if (startSweepFromMin) {
        Serial.println(F("(Min -> Max -> Min)"));
      } else {
        Serial.println(F("(Max -> Min -> Max)"));
      }
    }
  }

  // 動作モードごとの処理
  switch (currentMode) {
    case MODE_MANUAL:
      updateManualRPM();
      break;

    case MODE_SWEEP_PHASE1:
      {
        unsigned long elapsed = millis() - sweepStartTime;
        int startRPM = startSweepFromMin ? sweepMinRPM : sweepMaxRPM;
        int endRPM   = startSweepFromMin ? sweepMaxRPM : sweepMinRPM;

        if (elapsed >= sweepDuration) {
          setRPM(endRPM);
          currentMode = MODE_SWEEP_PHASE2;
          sweepStartTime = millis();
          Serial.println(F("[INFO] オートスイープ (折り返し)"));
        } else {
          long diff = (long)endRPM - (long)startRPM;
          long rpm = (long)startRPM + (diff * (long)elapsed) / (long)sweepDuration;
          setRPM((int)rpm);
        }
      }
      break;

    case MODE_SWEEP_PHASE2:
      {
        unsigned long elapsed = millis() - sweepStartTime;
        int startRPM = startSweepFromMin ? sweepMaxRPM : sweepMinRPM;
        int endRPM   = startSweepFromMin ? sweepMinRPM : sweepMaxRPM;

        if (elapsed >= sweepDuration) {
          setRPM(endRPM);
          
          // ★ここで指定回数に達したか判定する
          if (currentSweepCycle < sweepRepeatCount) {
            // まだ回数が残っていればPHASE1に戻って次の往復を開始
            currentSweepCycle++;
            currentMode = MODE_SWEEP_PHASE1;
            sweepStartTime = millis();
            
            Serial.print(F("\n[INFO] オートスイープ連続実行 ["));
            Serial.print(currentSweepCycle);
            Serial.print(F("/"));
            Serial.print(sweepRepeatCount);
            Serial.println(F("回目]"));
          } else {
            // 指定回数すべて終わったらマニュアルに戻る
            currentMode = MODE_MANUAL;
            lastAdcValue = -1; // VR値の再読み込みを強制
            Serial.println(F("\n[INFO] すべてのスイープ完了。定常回転モードに戻りました"));
          }
        } else {
          long diff = (long)endRPM - (long)startRPM;
          long rpm = (long)startRPM + (diff * (long)elapsed) / (long)sweepDuration;
          setRPM((int)rpm);
        }
      }
      break;
  }

  // ---------------------------------------------------------
  // 定期的なシリアル出力処理 (500msおきに非同期実行)
  // ---------------------------------------------------------
  if (millis() - lastSerialTime >= serialInterval) {
    lastSerialTime = millis();
    
    // 周波数(Hz)の計算: 1分間の回転数(RPM) / 60秒
    float frequencyHz = (float)currentRPM / 60.0;

    // シリアルモニタへの出力
    Serial.print(F("Mode: "));
    if (currentMode == MODE_MANUAL)       Serial.print(F("MANUAL   "));
    if (currentMode == MODE_SWEEP_PHASE1) Serial.print(F("SWEEP_PH1"));
    if (currentMode == MODE_SWEEP_PHASE2) Serial.print(F("SWEEP_PH2"));

    Serial.print(F(" | Frequency: "));
    Serial.print(frequencyHz, 2); 
    Serial.print(F(" Hz | Engine Speed: "));
    Serial.print(currentRPM);
    Serial.println(F(" RPM"));
  }
}

void updateManualRPM() {
  int adcValue = analogRead(VR_PIN);
  
  if (lastAdcValue == -1 || abs(adcValue - lastAdcValue) >= 3) {
    lastAdcValue = adcValue;
    int step = map(adcValue, 0, 1023, 10, 100);
    setRPM(step * 100);
  }
}

void setRPM(int rpm) {
  if (rpm < 100) rpm = 100;
  
  // 現在のRPMをシリアル表示用にグローバル変数に退避
  currentRPM = rpm;
  
  // 1カウント = 4us = 0.000004秒
  // カウント値 = 15,000,000 / RPM
  unsigned long counts = 15000000UL / rpm;
  
  // TOP値 (OCR1A) を更新
  OCR1A = counts - 1;
}