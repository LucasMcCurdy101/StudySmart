# StudySmart

StudySmart helps StFX students find quiet places to study. It shows a floor plan of each building, with every study space marked Quiet, Moderate or Loud based on a noise sensor in the room.

This is a prototype built for the Group 58 design project (client: Teo, finding quiet study spaces on campus).

## What's in the prototype

- Building picker for Somers Hall and the Angus L. Macdonald Library
- Swipeable floor plans with a noise level for each study space
- "Quietest spots right now" list across campus
- Grey "No reading" state when a room's sensor stops sending
- Library opening hours, with a "Closed" state outside them

## Live sensor

One real sensor is installed: **Somers Hall, Group Study Room 312**. The website reads its latest 5-minute average from ThingSpeak channel [3577145](https://thingspeak.mathworks.com/channels/3577145) every 30 seconds. If no new reading arrives for 3 minutes, the room shows "No reading".

## Sample data

Every other room, the floor plans, room names and library hours are made-up examples that stay fixed.

## How the real system will work

1. An INMP441 microphone in each study room is wired to an ESP32 board.
2. The ESP32 measures the noise level every second and, every 30 seconds, sends the average of the last 5 minutes over Wi-Fi to ThingSpeak. It only measures volume and never records audio.
3. This website reads the latest value from ThingSpeak and shows it as Quiet (under 45 dB), Moderate (45–59 dB) or Loud (60 dB and up).

## Files

- `index.html` — the whole app in one file (no build step)
- `sensor/StudySmart_Sensor/StudySmart_Sensor.ino` — ESP32 code for the noise sensor (add your Wi-Fi details and ThingSpeak write key locally; never commit them)

## Team

Group 58: Lucas, Will, Zak, Oscar and Ben.
