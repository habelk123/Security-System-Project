# 📸 School Project: Security System

This was a project I worked on for half a semester in MECHTRON 2TA4, at McMaster University.
It's a mock security-system that utilizes a STM32F429ZI Discovery Board along with an OP344 Sound Sensor from SparkFun, to detect audible intrusion and alert the user via piezzo buzzer.

## 🧰 Tool's
 - STM32F429ZI Discovery Board
 - OP344 Sound Sensor
 - Piezobuzzer
 - Push Button's
 - Keil Studio Cloud (IDE)

## 💻Language/Framework
 - C++ (mBed OS)

## 💡 Features
 - Graphical UI for keypad to enable/disable system (via passcode), as well for displaying information to user
 - Touch-screen functionality for built in LCD display to register user input when entering passcode
 - Interrupt-driven button's to set states and enable functionality of the system
 - Timeout's/Ticker's to handle timing-sensitive logic, and ensure appropriate stateflow
 - Custom sound-sensor logic to handle sensing through voltage-spike detection

## Note:
Due to Keil Studio Cloud being deprecated in July 2026, this project may not be able to be executed through the VS Code extension for Keil Studio, hence a video link has been provided instead to demo the project.

[![Watch the Demo Video]](https://youtube.com/shorts/PzhdmcL7amo?feature=share)
