// #ifndef LoRa_h
// #define LoRa_h
// #pragma once

// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"
// #include "esp_system.h"
// #include <esp_log.h>
// #include "driver/spi_master.h"
// #include "driver/gpio.h"

// // --- SX126x Commands ---
// #define SX126X_CMD_SET_SLEEP                  0x84
// #define SX126X_CMD_SET_STANDBY                0x80
// #define SX126X_CMD_SET_FS                     0xC1
// #define SX126X_CMD_SET_TX                     0x83
// #define SX126X_CMD_SET_RX                     0x82
// #define SX126X_CMD_STOP_TIMER_ON_PREAMBLE     0x9F
// #define SX126X_CMD_SET_RX_DUTY_CYCLE          0x94
// #define SX126X_CMD_SET_CAD                    0xC5
// #define SX126X_CMD_SET_TX_CONTINUOUS_WAVE     0xD1
// #define SX126X_CMD_SET_TX_INFINITE_PREAMBLE   0xD2
// #define SX126X_CMD_SET_REGULATOR_MODE         0x96
// #define SX126X_CMD_CALIBRATE                  0x89
// #define SX126X_CMD_CALIBRATE_IMAGE            0x98
// #define SX126X_CMD_SET_PA_CONFIG              0x95
// #define SX126X_CMD_SET_RX_TX_FALLBACK_MODE    0x93
// #define SX126X_CMD_WRITE_REGISTER             0x0D
// #define SX126X_CMD_READ_REGISTER              0x1D
// #define SX126X_CMD_WRITE_BUFFER               0x0E
// #define SX126X_CMD_READ_BUFFER                0x1E
// #define SX126X_CMD_SET_DIO_IRQ_PARAMS         0x08
// #define SX126X_CMD_GET_IRQ_STATUS             0x12
// #define SX126X_CMD_CLEAR_IRQ_STATUS           0x02
// #define SX126X_CMD_SET_DIO2_AS_RF_SWITCH_CTRL 0x9D
// #define SX126X_CMD_SET_DIO3_AS_TCXO_CTRL      0x97
// #define SX126X_CMD_SET_RF_FREQUENCY           0x86
// #define SX126X_CMD_SET_PACKET_TYPE            0x8A
// #define SX126X_CMD_GET_PACKET_TYPE            0x11
// #define SX126X_CMD_SET_TX_PARAMS              0x8E
// #define SX126X_CMD_SET_MODULATION_PARAMS      0x8B
// #define SX126X_CMD_SET_PACKET_PARAMS          0x8C
// #define SX126X_CMD_GET_RX_BUFFER_STATUS       0x13
// #define SX126X_CMD_GET_PACKET_STATUS          0x14
// #define SX126X_CMD_GET_RSSI_INST              0x15
// #define SX126X_CMD_GET_STATS                  0x10
// #define SX126X_CMD_GET_DEVICE_ERRORS          0x17
// #define SX126X_CMD_CLEAR_DEVICE_ERRORS        0x07
// #define SX126X_CMD_SET_BUFFER_BASE_ADDRESS    0x8F

// // --- IRQ Masks ---
// #define IRQ_TX_DONE_MASK                      0x0001
// #define IRQ_RX_DONE_MASK                      0x0002
// #define IRQ_PREAMBLE_DETECTED_MASK            0x0004
// #define IRQ_SYNC_WORD_VALID_MASK              0x0008
// #define IRQ_HEADER_VALID_MASK                 0x0010
// #define IRQ_HEADER_ERR_MASK                   0x0020
// #define IRQ_CRC_ERR_MASK                      0x0040
// #define IRQ_CAD_DONE_MASK                     0x0080
// #define IRQ_CAD_DETECTED_MASK                 0x0100
// #define IRQ_TIMEOUT_MASK                      0x0200

// // --- Packet Types ---
// #define PACKET_TYPE_GFSK                      0x00
// #define PACKET_TYPE_LORA                      0x01

// // --- Standby Modes ---
// #define STDBY_RC                              0x00
// #define STDBY_XOSC                            0x01

// class LoRa
// {
//  public:
//     // Updated Constructor: Added 'busy' pin argument
// 	LoRa( int mosi, int miso, int clk, int cs, int reset, int dio, int busy, int power );
    
// 	int parsePacket(int size = 0);
// 	int read();
// 	void receive(int size = 0);
// 	int available();

//     // Sends a packet
// 	int beginPacket(int implicitHeader = 0);
// 	size_t write(const uint8_t *buffer, size_t size);
// 	int endPacket(bool async);
    
//     // Packet Info
// 	int getPacketRssi();
//     float getPacketSnr(); // Returns SNR in dB

//     // Configuration
//     void setFrequency(long frequency);
// 	void setSpreadingFactor(int sf);
// 	void setSignalBandwidth(long sbw);
//     void setCodingRate4(int denominator);
//     void setPreambleLength(long length);
//     void setSyncWord(int sw);
//     void setCRC( bool crc );
//     void setTxPower(int8_t power, int8_t outputPin);
//     void enableInvertIQ();
//     void disableInvertIQ();

//     // Mode Control
// 	void sleep();
// 	void idle(); // Standby

//     // Hardware Init
// 	void initializeSPI( int mosi, int miso, int clk, int cs );
// 	void initializeReset( int reset );
// 	void initializeDIO( int dio );
//     void initializeBUSY( int busy );
// 	void initialize( int power );

//     // IRQ Handler Support
// 	void setDataReceived( bool r )	{ _dataReceived = r; }
// 	bool getDataReceived()	{ return _dataReceived; }

//  protected:
//     // Low Level SPI for SX126x
//     void writeCommand(uint8_t opCode, uint8_t *data, uint16_t size);
//     void readCommand(uint8_t opCode, uint8_t *data, uint16_t size);
//     void writeRegister(uint16_t address, uint8_t value);
//     uint8_t readRegister(uint16_t address);
//     void writeBuffer(uint8_t offset, uint8_t *data, uint8_t length);
//     void readBuffer(uint8_t offset, uint8_t *data, uint8_t length);
    
//     // Helpers
//     void waitForBusy();
//     void fixLoRaPaClamp();
//     void updateModulationParams();
//     void updatePacketParams();

//  private:
// 	void delay( int delay );

// 	spi_device_handle_t 	_spi;
//     gpio_num_t              _busyPin;
//     gpio_num_t              _resetPin;
    
// 	int 					_packetIndex = 0;
//     int                     _payloadLen = 0;
// 	bool					_dataReceived = false;
	
//     // Cached Settings
//     long					_frequency;
//     int                     _sf;
//     long                    _bw;
//     int                     _cr;
//     long                    _preambleLen;
//     bool                    _crcOn;
//     bool                    _invertIq;
//     int                     _syncWord;
//     bool                    _implicitHeader;
// };

// #endif