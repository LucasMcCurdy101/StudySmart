# StudySmart

StudySmart helps StFX students find quiet places to study. It shows a floor plan of each building, with every study space marked Quiet, Moderate or Loud based on a noise sensor in the room.

This is a prototype built for the Group 58 design project (client: Teo, finding quiet study spaces on campus).

## What's in the prototype

- Building picker for Somers Hall and the Angus L. Macdonald Library
- Swipeable floor plans with a noise level for each study space
- "Quietest spots right now" list across campus
- Grey "No reading" state when a room's sensor stops sending
- Library opening hours, with a "Closed" state outside them

## Sample data

The floor plans, room names, library hours and all noise readings are made up for the prototype. A live demo toggle and Quieter / Louder buttons stand in for the real sensors.

## How the real system will work

1. An INMP441 microphone in each study room is wired to an ESP32 board.
2. The ESP32 measures the noise level and sends it over Wi-Fi to ThingSpeak every 30 seconds. It only measures volume and never records audio.
3. This website reads the latest value from ThingSpeak and shows it as Quiet (under 45 dB), Moderate (45–59 dB) or Loud (60 dB and up).

## Files

- `index.html` — the whole app in one file (no build step)

## Team

Group 58: Lucas, Will, Zak, Oscar and Ben.
