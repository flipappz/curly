# curly
A proof-of-concept system that leverages the YOLO object detection algorithm to identify characters in real time.

## Screen Detection

This project is a proof-of-concept application that uses the YOLO (You Only Look Once) object detection algorithm to identify and track human targets directly from the screen in real time.
It captures a region around the cursor, processes the image using a lightweight YOLOv4-tiny model via OpenCV’s DNN module, and returns the most confident detection.

## Requirements

### System
- Windows 10/11 (64-bit)
- ~200–300 MB free disk space (for model weights and dependencies)

### Development Environment
- C++17 compatible compiler (e.g., Visual Studio 2019 or newer)
- Windows SDK

### Libraries & Dependencies
- OpenCV (version 4.x recommended, with DNN module enabled)
- DirectX 11 (included with Windows SDK)
- Dear ImGui (for UI)

#### Required Libraries (Linker)
- d3d11.lib
- user32.lib
- gdi32.lib

## Use Cases
- Triggerbots externally (without touching memory)
- Aimbots/Aimlocks

Excuse me for the code, it's "bad".
