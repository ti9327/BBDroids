
#include <LibBB.h>

using namespace bb;

static const uint8_t PIN_DISABLE = 255;

// STEP 1
// Define your motor driver and pins here

//#define DIRECTIONPIN_MOTOR_DRIVER
#define DUALPWM_MOTOR_DRIVER
#if defined(USE_DIRECTIONPIN_MOTOR_DRIVER)
static const uint8_t PIN_DIR_A_1   = 2;
static const uint8_t PIN_DIR_B_1   = 3;
static const uint8_t PIN_DIR_PWM_1 = 19;
static const uint8_t PIN_ENABLE_1  = 20;
static const uint8_t PIN_DIR_A_2   = PIN_DISABLE;
static const uint8_t PIN_DIR_B_2   = PIN_DISABLE;
static const uint8_t PIN_DIR_PWM_2 = PIN_DISABLE;
static const uint8_t PIN_ENABLE_2  = PIN_DISABLE
DCMotor motor[2] = {
  DCMotor(PIN_DIR_A_1, PIN_DIR_B_1, PIN_DIR_PWM_1, PIN_ENABLE_1), 
  DCMotor(PIN_DIR_A_2, PIN_DIR_B_2, PIN_DIR_PWM_2, PIN_ENABLE_2)
  };
#elif defined(DUALPWM_MOTOR_DRIVER)
static const uint8_t PIN_PWM_A_1   = 18;
static const uint8_t PIN_PWM_B_1   = 19;
static const uint8_t PIN_PWM_A_2   = 3;
static const uint8_t PIN_PWM_B_2   = 2;
DCMotor motor[2] = {
  DCMotor(PIN_PWM_A_1, PIN_PWM_B_1), 
  DCMotor(PIN_PWM_A_2, PIN_PWM_B_2)
  };
#else
#error No valid motors defined!
#endif

// STEP 2
// Define your encoder pins here

static const uint8_t PIN_ENC_A_1 = 17;
static const uint8_t PIN_ENC_B_1 = 16;
static const uint8_t PIN_ENC_A_2 = 7;
static const uint8_t PIN_ENC_B_2 = 6;

bb::Encoder input[2] = {bb::Encoder(PIN_ENC_A_1, PIN_ENC_B_1), bb::Encoder(PIN_ENC_A_2, PIN_ENC_B_2)};

// STEP 3
// Compile and upload and connect via serial monitor!

// --------------------------------------------------

PIDController control[2] = {PIDController(input[0], motor[0]), PIDController(input[1], motor[1])};

const uint8_t MOTOR_0_FLAG = 1;
const uint8_t MOTOR_1_FLAG = 2;
int useMotor = 0;

float goal = 0.0;

const uint8_t MODE_PWM = 0;
const uint8_t MODE_SPEED =   1;
const uint8_t MODE_POSITION = 2;
int mode = 0;

const uint8_t OUTPUT_SERIALPLOTTER = 0;
const uint8_t OUTPUT_SERIALANALYZER = 0;
int outputMode = 0;

const uint8_t UNIT_MM = 0;
const uint8_t UNIT_TICKS = 1;
const uint8_t UNIT_FAKE = 2;
int unit = 1;

#define DROID_DO
//#define DROID_BB8

int pwmSpeedReduction = 0;

#if defined(DROID_DO)
// Values for D-O. This gives about 0.14mm per tick.
static const float WHEEL_CIRCUMFERENCE = 722.566310325652445;
static const float WHEEL_TICKS_PER_TURN = 979.2 * (97.0/18.0); // 979 ticks per one turn of the drive gear, 18 teeth on the drive gear, 96 teeth on the main gear.
float speedKp = 0.13, speedKi = 0.8, speedKd = 0.0;
float speedCutoff = 25;
float posKp = 0.05, posKi = 0.0, posKd = 0.0;
float posCutoff = 25;
#endif

#if defined(DROID_BB8)
// Values for BB-8. This gives about 0.33mm per tick.
static const float WHEEL_CIRCUMFERENCE     = 2*M_PI*253.0; // bb8 motor pwm 0
static const float WHEEL_TICKS_PER_TURN    = 4776.384;
float speedKp = 0.075, speedKi = 0.2, speedKd = 0.0;
float posKp = 0.027, posKi = 0.005, posKd = 0.0;
float speedCutoff = 25;
float posCutoff = 25;
#endif

static const float MM_PER_TICK = WHEEL_CIRCUMFERENCE / WHEEL_TICKS_PER_TURN;

class DCMotorTest: public bb::Subsystem {
public:
  Result initialize() {
    name_ = "dcmotortest";
    description_ = "DC Motor Test";
    help_ = 
"BBDROIDS: DC Motor Control Tester\n"\
"=================================\n"\
"Commands:\n"\
"    help                     Print this help\n"\
"    start                    Start the test\n"\
"    stop                     Stop the test\n"\
"    add_goal <delta>         Add <delta> to current goal\n"\
"    reset                    Reset controllers\n"\
"While the test is running, information about controller state will be output in a format suited for\n"\
"the Arduino serial plotter. While it is running, you can either enter \"stop\" to stop the test, or\n"\
"enter numerical setpoints.\n";

    addParameter("useMotor", "0: none, 1: motor 0, 2: motor 1, 3: both", useMotor, 0, 3);
    addParameter("mode", "0: PWM, 1: speed, 2: position", mode, 0, 2);
    addParameter("outputMode", "0: output for Arduino Serial Plotter , 1: output for Curio Res Serial Analyzer", outputMode, 0, 1);
    addParameter("unit", "0: millimeters, 1: ticks, 2: 'fake' (goal is in ticks, but converted to mm)", unit, 0, 2);
    addParameter("speedKp", "P constant for speed control", speedKp);
    addParameter("speedKi", "I constant for speed control", speedKi);
    addParameter("speedKd", "D constant for speed control", speedKd);
    addParameter("posKp", "P constant for position control", posKp);
    addParameter("posKi", "I constant for position control", posKi);
    addParameter("posKd", "D constant for position control", posKd);
    addParameter("goal", "PWM, speed, or position goat", goal);
    addParameter("speedCutoff", "Cutoff frequency for speed filter (Hz)", speedCutoff);
    addParameter("posCutoff", "Cutoff frequency for position filter (Hz)", posCutoff);
    addParameter("pwmSpeedReduction", "Speed reduction on PWM", pwmSpeedReduction, 0, 4);

    input[0].setMillimetersPerTick(WHEEL_CIRCUMFERENCE / WHEEL_TICKS_PER_TURN);
    input[1].setMillimetersPerTick(WHEEL_CIRCUMFERENCE / WHEEL_TICKS_PER_TURN);

    return Subsystem::initialize();    
  }

  Result start(ConsoleStream *stream) {
    started_ = true;
    operationStatus_ = RES_OK;
    #if 0
    REG_TCC0_CTRLA |= TCC_CTRLA_PRESCALER_DIV16 | 
                  TCC_CTRLA_ENABLE;  
    #endif


    control[0].reset();
    control[1].reset();
    motor[0].setEnabled(true);
    motor[1].setEnabled(true);
    return RES_OK;
  }
  
  Result stop(ConsoleStream *stream) {
    started_ = false;
    operationStatus_ = RES_SUBSYS_NOT_STARTED;
    motor[0].set(0);
    motor[1].set(0);
    motor[0].setEnabled(false);
    motor[1].setEnabled(false);
    return RES_OK;
  }

  Result step() {
#if 0
    TCC0->PER.reg = (0xffff)>>pwmSpeedReduction;
    TCC1->PER.reg = (0xffff)>>pwmSpeedReduction;
    analogWriteResolution(8+pwmSpeedReduction);
#endif

    for(int i=0; i<2; i++) {
      input[i].setSpeedFilterCutoff(speedCutoff);
      input[i].setPositionFilterCutoff(posCutoff);
    }

    if(outputMode == OUTPUT_SERIALPLOTTER) {
      Console::console.printfBroadcast("Goal:%f", goal);
    } else {
      Console::console.printfBroadcast("%f", goal);
    }

    if(mode == MODE_PWM) {
      float g = constrain(goal, -255.0, 255.0);
      for(int i=0; i<2; i++) {
        if(useMotor & 1<<i) {
          motor[i].set(g);
          input[i].update();

          if(outputMode == OUTPUT_SERIALPLOTTER)
            Console::console.printfBroadcast(",RawEnc%d:%f", i, input[i].present(bb::Encoder::INPUT_SPEED));
          else {
            Console::console.printfBroadcast("%f %f", 
                                             input[i].present(bb::Encoder::INPUT_SPEED, true), 
                                             input[i].present(bb::Encoder::INPUT_SPEED, false));
          }
        }
      }
      Console::console.printfBroadcast("\n");
    } 
    
    else if(mode == MODE_SPEED) {
      for(int i=0; i<2; i++) {
        float g = goal;

        input[i].setMode(bb::Encoder::INPUT_SPEED);
        if(unit == UNIT_MM) input[i].setUnit(bb::Encoder::UNIT_MILLIMETERS);
        else if(unit == UNIT_TICKS) input[i].setUnit(bb::Encoder::UNIT_TICKS);
        else if(unit == UNIT_FAKE) {
          input[i].setUnit(bb::Encoder::UNIT_MILLIMETERS);
          g = goal * MM_PER_TICK;
        }

        control[i].setControlParameters(speedKp, speedKi, speedKd);
        control[i].setGoal(g);
        
        if(useMotor & 1<<i) {
          control[i].update();

          if(outputMode == OUTPUT_SERIALPLOTTER) {
            Console::console.printfBroadcast(",Speed%d:%f", i, input[i].present(bb::Encoder::INPUT_SPEED));
          } else {
            Console::console.printfBroadcast(" %f %f", 
                                            input[i].present(bb::Encoder::INPUT_SPEED, true),
                                            input[i].present(bb::Encoder::INPUT_SPEED));
          }
        }
      }
      Console::console.printfBroadcast("\n");
    } 
    
    else if(mode == MODE_POSITION) {
      if(outputMode == OUTPUT_SERIALPLOTTER)
        Console::console.printfBroadcast("Goal:%f", goal);
      for(int i=0; i<2; i++) {
        input[i].setMode(bb::Encoder::INPUT_POSITION);
        input[i].setUnit((bb::Encoder::Unit)unit);
        control[i].setControlParameters(posKp, posKi, posKd);
        control[i].setGoal(goal);
        if(useMotor & 1<<i) {

          control[i].update();
          if(outputMode == OUTPUT_SERIALPLOTTER) {
            Console::console.printfBroadcast(",Pos:%f", input[i].present(bb::Encoder::INPUT_POSITION, true));
          } else {
            Console::console.printfBroadcast(" %f %f", 
                                            input[i].present(bb::Encoder::INPUT_POSITION, true),
                                            input[i].present(bb::Encoder::INPUT_POSITION));
          }
        } 
      }
      Console::console.printfBroadcast("\n");
    }

    return RES_OK;
  }
  
  Result handleConsoleCommand(const std::vector<String>& words, ConsoleStream *stream) {
    if(words.size() == 0) return RES_CMD_UNKNOWN_COMMAND;

    if(words[0] == "help") {
      stream->printf(help_);
      printParameters(stream);
      return RES_OK;
    }

    if(words[0] == "start") {
      start(stream);
      return RES_OK;
    }

    if(words[0] == "stop") {
      stop(stream);
      return RES_OK;
    }

    if(words[0] == "reset") {
      for(int i=0; i<2; i++) control[i].reset();
      return RES_OK;
    }

    if(words[0] == "add") {
      if(words.size() != 2) return RES_CMD_INVALID_ARGUMENT_COUNT;
      goal += words[1].toFloat();
      return RES_OK;
    }

    if(isStarted() && words.size() == 1) {
      bool isNumber = true;
      for(unsigned int i=0; i<words[0].length(); i++) {
        if(!isDigit(words[0].charAt(i)) && words[0].charAt(i) != '.' && words[0].charAt(i) != '-') {
          isNumber = false;
          break;
        }
      }
      if(isNumber) {
        float g = words[0].toFloat();
        if(mode == MODE_PWM && (g < -255 || g > 255)) return RES_COMMON_OUT_OF_RANGE;
        goal = g;
        return RES_OK;
      }
    }

    return Subsystem::handleConsoleCommand(words, stream);
  }
};

DCMotorTest sut;

void setup() {
  pinMode(15, OUTPUT);
  digitalWrite(0, LOW);

  Serial.begin(200000);
  while(!Serial);

  Console::console.initialize(200000);
  Runloop::runloop.initialize();
  Runloop::runloop.setCycleTimeMicros(10000);
  sut.initialize();

  Console::console.setFirstResponder(&sut);
  Console::console.start();
  Runloop::runloop.start();
}

void loop() {}