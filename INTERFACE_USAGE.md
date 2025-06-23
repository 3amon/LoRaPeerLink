# LoRa Link Interface Usage

This document demonstrates how to use the new `ILoRaLink` interface that provides a common abstraction for both `LoRaBasicLink` and `LoRaBackoffLink` implementations.

## Basic Usage

```cpp
#include "ILoRaLink.h"
#include "LoraBasicLink.h"
#include "LoraBackoffLink.h"

// Create links through the interface
std::unique_ptr<ILoRaLink> basicLink = 
    std::make_unique<LoRaBasicLink>(&radio, nodeId, getTime, sleep);

std::unique_ptr<ILoRaLink> backoffLink = 
    std::make_unique<LoRaBackoffLink>(&radio, nodeId, getTime, sleep);

// Use either implementation through the same interface
uint8_t message[] = "Hello World";
basicLink->sendPacket(destId, message, sizeof(message));
backoffLink->sendPacket(destId, message, sizeof(message));
```

## Polymorphic Usage

```cpp
// Function that works with any LoRa link implementation
void sendBroadcast(ILoRaLink* link, const char* message) {
    uint8_t msg[] = message;
    link->sendPacket(0xFF, msg, strlen(message)); // 0xFF = broadcast
}

// Can be used with either implementation
LoRaBasicLink basicLink(&radio, 1, getTime, sleep);
LoRaBackoffLink backoffLink(&radio, 1, getTime, sleep);

sendBroadcast(&basicLink, "Hello from basic link");
sendBroadcast(&backoffLink, "Hello from backoff link");
```

## RollCall Integration

The `RollCall` class now uses the interface, so it can work with either link type:

```cpp
// Works with basic link
LoRaBasicLink basicLink(&radio, 1, getTime, sleep);
RollCall rollCall(&basicLink, "node-name", getTime, sleep);

// Also works with backoff link
LoRaBackoffLink backoffLink(&radio, 1, getTime, sleep);  
RollCall rollCall2(&backoffLink, "node-name", getTime, sleep);
```

## Benefits

- **Flexibility**: Switch between link implementations without changing application code
- **Extensibility**: Easy to add new LoRa link types that implement the interface
- **Testability**: Mock implementations can be created for testing
- **Clean Architecture**: Reduces coupling between layers