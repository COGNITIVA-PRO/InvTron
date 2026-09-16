# invtron
## An ESP32-powered Inventory Control using Firebase

## Purpose
Control inventory of parts and pieces of my electronics lab, storing data on the cloud and showing an image of the selected item.

## Additions to original version
This solution was based on Akashdeep Singh's system as seen on his youtube channel (The Wrench) and the original idea can be found on <a href="https://www.youtube.com/watch?v=92hJQUBDqQs">**Youtube**</a> and <a href="https://cults3d.com/en/3d-model/gadget/i-built-a-smart-inventory-system-for-my-workshop-arduino-uno-q">**Cults3D**</a>.

Akash's version was intended to run on a more robust hardware (the Arduino Q, which is a Linux SBC with an integrated arduino sidekick).
I didn't want to spend over $59 on a new Arduino Q, so I built this version to run on an **ESP32** and used **Firebase** to store the data on the cloud, reducing cost and enabling mobility.

I had a bunch of ESP32, displays and modules available on my lab, so I made some changes to the gadget's enclosure to better accommodate them. The modified 3D model is also available on this git repository.

Another addition to this version is a CRUD (_Create, Read, Update, Delete_) companion **webapp** used to easily maintain the item list and related data.

## Data structure
The original solution was intended to run on Arduino Q, so the data was being persisted locally using SQLite. I wanted to have more control and flexibility over data, perhaps with a CRUD app that could either run on my laptop or mobile.

Having this in mind created a Firebase Readtime Database instance and used the original Json to mass upload the data to Firebase. I also made some modifications to the data structure, like different entities and extended hierarchy, since I wanted more details for the profusion of items I have on my lab.

The new structure comprehends:
* Class - groups the classes an item pertains to (e.g. Components, Modules, Devices, PCB Boards etc.) 
* Category - groups items of same kind under a large umbrella (e.g. Display, ESP32, Cameras, Capacitors, Resistors etc.)
* Subcategory - groups items of a givem Category that have similar characteristics (e.g. TFT Graphic Displays, OLED Graphic Displays, LED Displays, LCD Displays, Development Boards, Electrolictic Capacitors, Ceramic Capacitors etc.)
* Items - describe the item's Part. No. and Name, along with its main characteristics (like Volts, Ohms, Current, Gain, etc.), a link to its datasheet or website, a link to its image thumbnail and its location.
* Inventory - stores the number of pieces available, and is the only data updated by the gadget's firmware.

## Solution
The original idea was composed by the hardware (the gadget) and its firmware solelly, which was divided into a C++ app (for the Arduino components and modules) and a Python script (for data manipulation).

This version, on the other side, is composed by three parts:
* the gadget: ESP32+display+buttons/rotary encoder, battery, battey charger and 3D-printed enclosure
* the firmware: a simple ESP32 C++ app, controlling the ESP32 and enabling the user experience
* the webapp: a simple and self-contained Javascript CRUD application.

Since all data is persisted on Firebase in the cloud, the webapp can run on any PC, tablet or smartphone. If you have an Android device, you can also install it as an (web) app.


## Pre-reqs
In order to run this solution, you need to create a Firebase Realtime Database for yourself, and also **create and populate** the file <code>secret/credentials.h</code> with your own data, as shown below:
<pre>
// Wi-Fi credentials:
#define WIFI_SSID     "Your Wfi-Fi AP name"
#define WIFI_PASSWORD "Your Wfi-Fi password"

// Firebase credentials:
#define MY_API_KEY       "your API key from Firebase website"
#define MY_DATABASE_URL  "your Database URL from Firebase website"
#define MY_USER_EMAIL    "your own user email provided when creating the Firebase instance"
#define MY_USER_PASS     "your Firebase user password" 
</pre>
