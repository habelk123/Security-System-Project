
/**********************************************************************
 *  Project: Mock Security System (STM32F429ZI)
 *  Author: Habel Kingson
 *  Institution: McMaster University
 *  Course: Embedded Systems Design I
 *  Semester: Winter 2025
 *  
 *  NOTE: This code is part of an academic project. 
 *  Sharing or republishing without attribution is not permitted.
 **********************************************************************/

#include "mbed.h"
#include "LCD_DISCO_F429ZI.h"
#include "TS_DISCO_F429ZI.h"
#include "stm32f429i_discovery_ts.h"
#include <cstdint>
#include "DebouncedInterrupt.h"

//Initialize Board Utitlities
LCD_DISCO_F429ZI LCD;
TS_DISCO_F429ZI TS;

DigitalOut led3(PG_13);
DigitalOut led4(PG_14);
DigitalOut buzzer(PD_14);
AnalogIn soundSensor(PA_5);

//External Interrupts:
InterruptIn userButton(BUTTON1);
DebouncedInterrupt exButton1(PC_3);
DebouncedInterrupt exButton2(PC_14);

//Tickers and Timeouts:
Timeout startup; //for indicating when to clear the startup message
Timeout delayKeyStroke; //for debouncing touchscreen inputs
Timeout wrongPassword; //for indicating when to clear the "WRONG PASSWORD!" message
Timeout changeCode; //for indicating when to clear the pass code reset message
Timeout confirmPassCode; //for indicating when to clear the confirm pass code message
Ticker delaySoundRead; //for sampling voltage readings from the sound sensor every 10 ms
Ticker micInitSample; //for sampling an initial 50 readings every 5 ms, to having an averaged value at system startup 
Ticker greenLED; //control green led flash
Ticker redLED; //control red led flash
Ticker buzz; //control piezzo buzzer noise


//Table of FSM States
typedef enum {SET_CODE = 0,CONFIRM_CODE, DISARMED, ARMED, TRIGGERED} State_Type;

//State Function Prototypes
void setCode(void);
void confirmCode(void);
void disarmed(void);
void armed(void);
void triggered(void);
void initializeSM(void);

//Array of Function Pointers
static void (*state_table[])(void) = {setCode, confirmCode, disarmed, armed, triggered};

//Global Variables
static State_Type fsm_state;
TS_StateTypeDef TS_State; //for retriveing touch screen state data
volatile bool delayFlag = false; //flag for debouncing multiple touch inputs within touchInput()
volatile bool clearStartMessage = false; //flag to clear the startup message
volatile bool clearResetCodeMessage = false; //flag to clear the reset pass code message
volatile bool clearConfirmCodeMessage = false; //flag to clear the confirmation of passcode message
volatile bool enableTouch; //flag to enable touchInput() when required
volatile bool keypadVisible; //flag to allow for processing of touch logic within an FSM state
volatile bool clearDisarmMsg = false; //flag to clear the disarm message
volatile bool resettingCode = false; //flag for enabling passcode reset logic
volatile bool wrongPassMessage = false; //flag to clear the wrong passcode message


//Passcode Entry Variables and Flags
int digitCount = 0; //tracking entry (max of 3)
int numPressed; //assigning an integer value to keys 0-6 when pressed
volatile bool del; //flag for enabling delete key
volatile bool ent; //flag for enabling enter key

//Passcode Storage
int passCodeStore[3]; //storing confirmed passcode until reset
int passCodeConfirm[3]; //temporary storage to confirm passcode 
int passCodeInput[3]; //for holding an input password when required

//Sound Sensor  Variables and Flags
volatile bool readSoundSensor; //flag for enabling sensor reading
float soundSum; //for initializing a value for lastSample before reading sensor data
int soundSamples; //for initializing a value for lastSample
const int numInitSamples = 50; //for initialzing a value for lastSample before reading sensor data
volatile bool soundInitComplete; //flag for when to actually begin process voltage readings
volatile bool sampleSoundFlag; //flag for when to determine lastSample before reading sensor data
float currentSample = 0; //current voltage reading
float lastSample; //previous voltage reading
const float threshold = 1.0; //threshold setting
float change = 0; //difference between current and previous reading

void generateKeyPad();

//Buttton ISR's
void setStateArm(){ //user button press will toggle the keypad and the touch input for arming
    if(fsm_state == DISARMED){
        LCD.Clear(LCD_COLOR_WHITE);
        generateKeyPad();
        enableTouch = true;
        keypadVisible = true; 
    }
} 

void resetAlarm(){ //external button 1 will allow for disabling the the alarm when the correct passcode is inputed
    if(fsm_state == TRIGGERED || fsm_state == ARMED){
        LCD.Clear(LCD_COLOR_WHITE);
        generateKeyPad();
        delaySoundRead.detach(); //stop reading sensor data if just set (ARMED state transition)
        enableTouch = true;
        keypadVisible = true; 
        digitCount = 0;
        passCodeInput[0] = -1;
        passCodeInput[1] = -1;
        passCodeInput[2] = -1;
        soundSum = 0;
        soundSamples = 0;
        change = 0;
        currentSample = 0;
    }
}

void changePassCode(){ //external button 2 will allow for resetting the passcode when in the DISARMED state, if the user forgot or would like to change the passcode
    if(fsm_state == DISARMED){
        LCD.Clear(LCD_COLOR_WHITE);
        resettingCode = true;
        digitCount = 0;
        passCodeStore[0] = -1;
        passCodeStore[1] = -1;
        passCodeStore[2] = -1;
    }
}

//Utility Functions
void clearKeyPad(){ //clear keypad from LCD screen
    LCD.SelectLayer(0);
    LCD.Clear(LCD_COLOR_WHITE);
    LCD.SelectLayer(1);
    LCD.Clear(LCD_COLOR_WHITE);
}

void generateKeyPad(){ //keypad generation of LCD screen

    LCD.SelectLayer(0);
    LCD.Clear(LCD_COLOR_WHITE);

    //Row 4(DEL,ENT) Colours
    LCD.SetTextColor(LCD_COLOR_RED);
    LCD.FillRect(0,240,80,80);
    
    LCD.SetTextColor(LCD_COLOR_GREEN);
    LCD.FillRect(160,240,80,80);
    
    LCD.SetTextColor(LCD_COLOR_BLACK);
    //Row 1(1,2,3)
    LCD.DrawRect(0,80,80,80);
    LCD.DrawRect(80,80,80,80);
    LCD.DrawRect(160,80,80,80);
    //Row 2(4,5,6)
    LCD.DrawRect(0,160,80,80);
    LCD.DrawRect(80,160,80,80);
    LCD.DrawRect(160,160,80,80);
    //For Row 3 (0)
    LCD.DrawRect(80,240,80,80);

    LCD.SelectLayer(1); //Draw text layer
    LCD.Clear(0xFFFFFF); //clear residual artifacts
    LCD.SetColorKeying(1, 0xFFFFFF); //Let Text Layer be transparent to background
    LCD.SetLayerVisible(1, ENABLE); //displaying text layer
    LCD.SetTextColor(LCD_COLOR_BLACK); //for text color
    LCD.SetBackColor(0xFFFFFF); //make white background behind text transparent due to previous keying

    //Display keys
    LCD.DisplayStringAt(35, 115, (uint8_t * )"1", LEFT_MODE);
    LCD.DisplayStringAt(115, 115, (uint8_t * )"2", LEFT_MODE);
    LCD.DisplayStringAt(195, 115, (uint8_t * )"3", LEFT_MODE);
    LCD.DisplayStringAt(35, 195, (uint8_t * )"4", LEFT_MODE);
    LCD.DisplayStringAt(115, 195, (uint8_t * )"5", LEFT_MODE);
    LCD.DisplayStringAt(195, 195, (uint8_t * )"6", LEFT_MODE);
    LCD.DisplayStringAt(17, 275, (uint8_t * )"DEL", LEFT_MODE);
    LCD.DisplayStringAt(115, 275, (uint8_t * )"0", LEFT_MODE);
    LCD.DisplayStringAt(177, 275, (uint8_t * )"ENT", LEFT_MODE);

}

void resetDelayFlag(){ //for setting delay flag
    delayFlag = false;
}

void touchInput(){ //touch input on keypad keys
    TS.GetState(&TS_State);
    numPressed = -1; del = ent = false; //reset each time when called in main loop, to prevent carrying over previosu values
    if(TS_State.TouchDetected && !delayFlag){
        delayFlag = true; //flag to debounce
        //Registering Touch Inputs for (1,2,3)
        if((TS_State.X >= 0 && TS_State.X < 80) && (TS_State.Y >= 160 && TS_State.Y < 240)){
            numPressed = 1;
        }
        else if((TS_State.X >= 80 && TS_State.X < 160) && (TS_State.Y >= 160 && TS_State.Y < 240)){
            numPressed = 2;
        }
        else if((TS_State.X >= 160 && TS_State.X < 240) && (TS_State.Y >= 160 && TS_State.Y < 240)){
            numPressed = 3;
        }

        //Registering Touch Inputs for (4,5,6)
        else if((TS_State.X >= 0 && TS_State.X < 80) && (TS_State.Y >= 80 && TS_State.Y < 160)){
            numPressed = 4;
        }
        else if((TS_State.X >= 80 && TS_State.X < 160) && (TS_State.Y >= 80 && TS_State.Y < 160)){
            numPressed = 5;
        }
        else if((TS_State.X >= 160 && TS_State.X < 240) && (TS_State.Y >= 80 && TS_State.Y < 160)){
            numPressed = 6;
        }

        //Registering Touch Inputs for (DEL,0,ENT)
        else if((TS_State.X >= 0 && TS_State.X < 80) && (TS_State.Y >= 0 && TS_State.Y < 80)){
            del = true;
        }
        else if((TS_State.X >= 80 && TS_State.X < 160) && (TS_State.Y >= 0 && TS_State.Y < 80)){
            numPressed = 0;
        }
        else if((TS_State.X >= 160 && TS_State.X < 240) && (TS_State.Y >= 0 && TS_State.Y < 80)){
            ent = true;
        }
        delayKeyStroke.attach(&resetDelayFlag, 250ms); //debouncing touchscreen inputs
    }   
}

void setMessageFlag(){ //for toggling the start message flag
    clearStartMessage = true;
}

void setResetMessageFlag(){ //for toggling the reset message flag
    clearResetCodeMessage = true;
}

void setConfirmMessageFlag(){ //for toggling the confirmation message flag
    clearConfirmCodeMessage = true;
}

void greenLEDFlash(){ //led3 control
    led3 = !led3;
}

void redLEDFlash(){ //led4 control
    led4 = !led4;
}

void wrongPssword(){ //for clearing the wrong password message
    wrongPassMessage = false;
    enableTouch = true;
    LCD.SetTextColor(LCD_COLOR_WHITE);
    LCD.FillRect(0, 40, 240, 40);
    LCD.SetTextColor(LCD_COLOR_BLACK);
}

void delaySoundSample(){ //for controlling sound sensor reading
    readSoundSensor = true;
}

void piezoBuzzer(){ //toggling piezobuzzer
    buzzer = !buzzer;
}

void collectInitSample(){ //enabling data collection for setting lastSample
    sampleSoundFlag = true;
}

void initializeFirstSample(){ //needed to ensure lastSample is set to a stable reading
    soundSum += soundSensor.read_voltage(); //add up all voltage readings
    soundSamples++; //increment for keeping track
    if(soundSamples >= numInitSamples){ //check for 50 samples
        lastSample = soundSum/numInitSamples; //set to the average of the 50
        //printf("Last Sample: %0.2f\n", lastSample); debug message for seeing what the last sample is set to when first beginning to read sensor data
        soundInitComplete = true; //indicate when to start processing sound logic for spikes
        micInitSample.detach(); //detach ticker that runs every 5ms to get initial data
        sampleSoundFlag = false; //stop enabling this function
    }
}

float readSound(){ //read data from sound sensor
    if(!soundInitComplete) return change = 0; //until the initial value of lastSample is set, dont process anything

    currentSample = soundSensor.read_voltage(); //store the current reading
    change = abs(currentSample - lastSample); //calculate the absolute difference between current and previous
    lastSample = currentSample; //update last sample
    return change; //return the difference
}

//State Function Implementations
void initializeSM(){
    fsm_state = SET_CODE;
    LCD.SetFont(&Font20);
    LCD.DisplayStringAt(0, 40, (uint8_t * )"Hello!", CENTER_MODE);
    LCD.DisplayStringAt(0, 120, (uint8_t * )"Enter a Passcode!", CENTER_MODE);
    LCD.DisplayStringAt(0, 200, (uint8_t * )"Must be 3 digits!", CENTER_MODE);
    startup.attach(&setMessageFlag, 2000ms);
}

void setCode(){
    if(keypadVisible){ //check if the keypad has been set
        if(digitCount == 3 && ent){ //if 3 digits have been entered, with the enter key
            fsm_state = CONFIRM_CODE;
            LCD.SetFont(&Font20);
            enableTouch = false;
            keypadVisible = false;
            digitCount = 0;
            clearKeyPad();

            LCD.SetTextColor(LCD_COLOR_BLACK);
            LCD.DisplayStringAt(0, 100, (uint8_t*)"PLEASE CONFIRM", CENTER_MODE);
            LCD.DisplayStringAt(0, 140, (uint8_t*)"PASSCODE", CENTER_MODE);
            confirmPassCode.attach(&setConfirmMessageFlag, 2000ms);
        }
        if(del){ //delete the last entry and show this on the keypad display
            if(digitCount > 0){
                digitCount--;
            }
            LCD.SetTextColor(LCD_COLOR_WHITE);
            LCD.FillRect(70 + (digitCount*40), 40, 24, 24);
        }
        if(digitCount < 3 && numPressed != -1){ //store the input value (keypress) and indicate on the display with an asterisk
            passCodeStore[digitCount] = numPressed;

            LCD.SetTextColor(LCD_COLOR_BLACK);
            LCD.SetFont(&Font24);
            LCD.DisplayStringAt(70 + (digitCount*40), 40, (uint8_t*)"*", LEFT_MODE);

            digitCount++;
        }
    }
}

void confirmCode(){
     if(keypadVisible){
        if(digitCount == 3 && ent){
            if(passCodeConfirm[0] == passCodeStore[0] && passCodeConfirm[1] == passCodeStore[1] && passCodeConfirm[2] == passCodeStore[2]){ //check if the passcodes match before confirming
                fsm_state = DISARMED;
                LCD.SetFont(&Font24);
                enableTouch = false;
                keypadVisible = false;
                digitCount = 0;
                //reset the data in the stored confirmation array
                passCodeConfirm[0] = -1;
                passCodeConfirm[1] = -1;
                passCodeConfirm[2] = -1;
                clearKeyPad();
                
                LCD.DisplayStringAt(0, 100, (uint8_t*)"SYSTEM IS", CENTER_MODE);
                LCD.SetTextColor(LCD_COLOR_GREEN);
                LCD.DisplayStringAt(0, 140, (uint8_t*)"DISARMED!", CENTER_MODE);
                LCD.SetTextColor(LCD_COLOR_BLACK);

                greenLED.attach(&greenLEDFlash, 1000ms); //flash at 1 Hz when transitioning to the disarmed state

            }
            else{
                if(!wrongPassMessage){ //if codes don't mach, indicate visually
                    digitCount = 0;
                    enableTouch = false;
                    wrongPassMessage = true;
                    LCD.SetFont(&Font20);
                    LCD.SetTextColor(LCD_COLOR_WHITE);
                    LCD.FillRect(0, 40, 240, 40);
                    LCD.SetTextColor(LCD_COLOR_BLACK);
                    LCD.DisplayStringAt(0, 40, (uint8_t*)"DON'T MATCH!", CENTER_MODE);
                    wrongPassword.attach(&wrongPssword, 1500ms); //for clearing the message after 1.5 seconds
                }
            }
        }
        if(del){
            if(digitCount > 0){
                digitCount--;
            }
            LCD.SetTextColor(LCD_COLOR_WHITE);
            LCD.FillRect(70 + (digitCount*40), 40, 24, 24);
        }
        if(digitCount < 3 && numPressed != -1){
            passCodeConfirm[digitCount] = numPressed;

            LCD.SetTextColor(LCD_COLOR_BLACK);
            LCD.SetFont(&Font24);
            LCD.DisplayStringAt(70 + (digitCount*40), 40, (uint8_t*)"*", LEFT_MODE);

            digitCount++;
        }
    }
}

void disarmed(){
    if(resettingCode){ //if changing the stored passcode
        fsm_state = SET_CODE;
        LCD.SetFont(&Font20);
        LCD.DisplayStringAt(0, 40, (uint8_t * )"Resetting", CENTER_MODE);
        LCD.DisplayStringAt(0, 80, (uint8_t * )"Passcode...", CENTER_MODE);
        changeCode.attach(&setResetMessageFlag, 3000ms); //clear reset message
        greenLED.detach();
        led3 = 0;
        resettingCode = false;
    }
    if(keypadVisible){
        if(digitCount == 3 && ent){
            if(passCodeInput[0] == passCodeStore[0] && passCodeInput[1] == passCodeStore[1] && passCodeInput[2] == passCodeStore[2]){ //check if the inputted password matches what is stored
                fsm_state = ARMED;
                LCD.SetFont(&Font24);
                enableTouch = false;
                keypadVisible = false;
                digitCount = 0;
                clearKeyPad();
                //display armed message
                LCD.DisplayStringAt(0, 100, (uint8_t*)"SYSTEM IS", CENTER_MODE);
                LCD.SetTextColor(LCD_COLOR_RED);
                LCD.DisplayStringAt(0, 140, (uint8_t*)"ARMED!", CENTER_MODE);
                LCD.SetTextColor(LCD_COLOR_BLACK);

                greenLED.detach();
                greenLED.attach(&greenLEDFlash, 2000ms); //slow down led flash to indicate to the user
                
                //begin enabling sensor logic
                delaySoundRead.attach(&delaySoundSample, 10ms);
                soundSum = 0.0;
                soundSamples = 0;
                soundInitComplete = false;
                sampleSoundFlag = false;
                micInitSample.attach(&collectInitSample, 5ms); //start with sampling an average value to set for lastSample
            }
            else{
                if(!wrongPassMessage){
                    digitCount = 0;
                    enableTouch = false;
                    wrongPassMessage = true;
                    LCD.SetFont(&Font20);
                    LCD.SetTextColor(LCD_COLOR_WHITE);
                    LCD.FillRect(0, 40, 240, 40);
                    LCD.SetTextColor(LCD_COLOR_BLACK);
                    LCD.DisplayStringAt(0, 40, (uint8_t*)"WRONG PASSWORD!", CENTER_MODE); //indicate the wrong password has been inputted
                    wrongPassword.attach(&wrongPssword, 1500ms);
                }
            }
        }
        if(del){
            if(digitCount > 0){
                digitCount--;
            }
            LCD.SetTextColor(LCD_COLOR_WHITE);
            LCD.FillRect(70 + (digitCount*40), 40, 24, 24);
        }
        if(digitCount < 3 && numPressed != -1){
            passCodeInput[digitCount] = numPressed;

            LCD.SetTextColor(LCD_COLOR_BLACK);
            LCD.SetFont(&Font24);
            LCD.DisplayStringAt(70 + (digitCount*40), 40, (uint8_t*)"*", LEFT_MODE);

            digitCount++;
        }
    }
}

void armed(){
    if(change > threshold){ //check if there was a spike larger than the preset threshold
        fsm_state = TRIGGERED;
        greenLED.detach();
        led3 = 0;
        redLED.attach(&redLEDFlash, 100ms); //flash red led at high frequency
        buzz.attach(&piezoBuzzer, 200us); //vibrate piezobuzzer at high frequency
        delaySoundRead.detach(); //stop reading sensor data
        readSoundSensor = false;
        LCD.Clear(LCD_COLOR_WHITE);
        LCD.DisplayStringAt(0, 100, (uint8_t*)"INTRUDER", CENTER_MODE); //display intruder detected message
        LCD.SetTextColor(LCD_COLOR_RED);
        LCD.DisplayStringAt(0, 140, (uint8_t*)"DETECTED!", CENTER_MODE);
    }
    
    if(keypadVisible){
        if(digitCount == 3 && ent){
            if(passCodeInput[0] == passCodeStore[0] && passCodeInput[1] == passCodeStore[1] && passCodeInput[2] == passCodeStore[2]){ //check if correct passcode has been entered
                fsm_state = DISARMED; //disarm system
                LCD.SetFont(&Font24);
                enableTouch = false;
                keypadVisible = false;
                digitCount = 0;
                clearKeyPad();
                
                LCD.DisplayStringAt(0, 100, (uint8_t*)"SYSTEM IS", CENTER_MODE);
                LCD.SetTextColor(LCD_COLOR_GREEN);
                LCD.DisplayStringAt(0, 140, (uint8_t*)"DISARMED!", CENTER_MODE);
                LCD.SetTextColor(LCD_COLOR_BLACK);

                
                greenLED.detach();
                greenLED.attach(&greenLEDFlash, 1000ms);
            }
            else{
                if(!wrongPassMessage){
                    digitCount = 0;
                    enableTouch = false;
                    wrongPassMessage = true;
                    LCD.SetFont(&Font20);
                    LCD.SetTextColor(LCD_COLOR_WHITE);
                    LCD.FillRect(0, 40, 240, 40);
                    LCD.SetTextColor(LCD_COLOR_BLACK);
                    LCD.DisplayStringAt(0, 40, (uint8_t*)"WRONG PASSWORD!", CENTER_MODE);
                    wrongPassword.attach(&wrongPssword, 1500ms);
                }
            }
        }
        if(del){
            if(digitCount > 0){
                digitCount--;
            }
            LCD.SetTextColor(LCD_COLOR_WHITE);
            LCD.FillRect(70 + (digitCount*40), 40, 24, 24);
        }
        if(digitCount < 3 && numPressed != -1){
            passCodeInput[digitCount] = numPressed;

            LCD.SetTextColor(LCD_COLOR_BLACK);
            LCD.SetFont(&Font24);
            LCD.DisplayStringAt(70 + (digitCount*40), 40, (uint8_t*)"*", LEFT_MODE);

            digitCount++;
        }
    }
}

void triggered(){
    if(keypadVisible){
        if(digitCount == 3 && ent){
            if(passCodeInput[0] == passCodeStore[0] && passCodeInput[1] == passCodeStore[1] && passCodeInput[2] == passCodeStore[2]){ //check if correct passcode has been entered
                buzz.detach(); //stop buzzer noise
                buzzer = 0;
                fsm_state = DISARMED; //disarm system
                LCD.SetFont(&Font24);
                enableTouch = false;
                keypadVisible = false;
                digitCount = 0;
                clearKeyPad();
                
                LCD.DisplayStringAt(0, 100, (uint8_t*)"SYSTEM IS", CENTER_MODE);
                LCD.SetTextColor(LCD_COLOR_GREEN);
                LCD.DisplayStringAt(0, 140, (uint8_t*)"DISARMED!", CENTER_MODE);
                LCD.SetTextColor(LCD_COLOR_BLACK);

                redLED.detach();
                led4 = 0;
                greenLED.attach(&greenLEDFlash, 1000ms);
            }
            else{
                if(!wrongPassMessage){
                    digitCount = 0;
                    enableTouch = false;
                    wrongPassMessage = true;
                    LCD.SetFont(&Font20);
                    LCD.SetTextColor(LCD_COLOR_WHITE);
                    LCD.FillRect(0, 40, 240, 40);
                    LCD.SetTextColor(LCD_COLOR_BLACK);
                    LCD.DisplayStringAt(0, 40, (uint8_t*)"WRONG PASSWORD!", CENTER_MODE);
                    wrongPassword.attach(&wrongPssword, 1500ms);
                }
            }
        }
        if(del){
            if(digitCount > 0){
                digitCount--;
            }
            LCD.SetTextColor(LCD_COLOR_WHITE);
            LCD.FillRect(70 + (digitCount*40), 40, 24, 24);
        }
        if(digitCount < 3 && numPressed != -1){
            passCodeInput[digitCount] = numPressed;

            LCD.SetTextColor(LCD_COLOR_BLACK);
            LCD.SetFont(&Font24);
            LCD.DisplayStringAt(70 + (digitCount*40), 40, (uint8_t*)"*", LEFT_MODE);

            digitCount++;
        }
    }
}

int main(){
    TS.Init(LCD.GetXSize(), LCD.GetYSize()); //initialize touchscreen border

    soundSensor.set_reference_voltage(3); //set reference voltage for ADC pin

    //Enable ISR's
    __enable_irq();
    userButton.fall(&setStateArm);
    exButton1.attach(&resetAlarm, IRQ_FALL, 500, true);
    exButton2.attach(&changePassCode, IRQ_FALL, 500, true);

    //initialize actuators
    led3 = 0;
    led4 = 0;
    buzzer = 0;

    
    
    //Touchscreen DeBug
    if(TS.Init(LCD.GetXSize(), LCD.GetYSize()) != TS_OK){
        led4 = 1;
    }
    
    initializeSM(); //start the FSM

    

    while(1){
        if(clearStartMessage){ //keep the start message up until toggled off
            LCD.Clear(LCD_COLOR_WHITE);
            clearStartMessage = false;
            generateKeyPad();
            enableTouch = true; //allow touch input 
            keypadVisible = true; //for processing touch input only when keypad is visisble
        }
        if(clearResetCodeMessage){ //keep the reset message up until toggled off
            LCD.Clear(LCD_COLOR_WHITE);
            clearResetCodeMessage = false;
            generateKeyPad();
            enableTouch = true;
            keypadVisible = true;
        }
        if(clearConfirmCodeMessage){ //keep the confirmation message up until toggled off
            LCD.Clear(LCD_COLOR_WHITE);
            clearConfirmCodeMessage = false;
            generateKeyPad();
            enableTouch = true;
            keypadVisible = true;
        }
        if(enableTouch){ //control when to enable touch input
            touchInput();
        }
        if(readSoundSensor){ //control when to read sensor data
            readSound();
            //printf("Sound Sensor Voltage Reading: %0.2f\n", currentSample); debug message for getting voltage readings from sound sensor
            readSoundSensor = false;
        }
        if(sampleSoundFlag){ //control when to initialize data for the last sample
            initializeFirstSample();
            sampleSoundFlag = false;
        }
        state_table[fsm_state](); //move through FSM
    }
}




