/**
 * @file Inteligence_Agriculture.ino
 * @author rakwireless.com
 * @brief This sketch demonstrate reading data from different agriculture sensors
 *    and send the data to lora gateway.
 * @version 0.1
 * @date 2020-07-28
 * 
 * @copyright Copyright (c) 2020
 * 
 * @note RAK5005-O GPIO mapping to RAK4631 GPIO ports
   RAK5005-O <->  nRF52840
   IO1       <->  P0.17 (Arduino GPIO number 17)
   IO2       <->  P1.02 (Arduino GPIO number 34)
   IO3       <->  P0.21 (Arduino GPIO number 21)
   IO4       <->  P0.04 (Arduino GPIO number 4)
   IO5       <->  P0.09 (Arduino GPIO number 9)
   IO6       <->  P0.10 (Arduino GPIO number 10)
   SW1       <->  P0.01 (Arduino GPIO number 1)
   A0        <->  P0.04/AIN2 (Arduino Analog A2
   A1        <->  P0.31/AIN7 (Arduino Analog A7
   SPI_CS    <->  P0.26 (Arduino GPIO number 26) 
 */
#include <ArduinoModbus.h> //http://librarymanager/All#ArduinoModbus     //http://librarymanager/All#ArduinoRS485

#include <Arduino.h>
#include <LoRaWan-RAK4630.h> //http://librarymanager/All#SX126x
#include <SPI.h>

// RAK4630 supply two LED
#ifndef LED_BUILTIN
#define LED_BUILTIN 35
#endif

#ifndef LED_BUILTIN2
#define LED_BUILTIN2 36
#endif

bool doOTAA = true;
#define SCHED_MAX_EVENT_DATA_SIZE APP_TIMER_SCHED_EVENT_DATA_SIZE /**< Maximum size of scheduler events. */
#define SCHED_QUEUE_SIZE 60										  /**< Maximum number of events in the scheduler queue. */
#define LORAWAN_DATERATE DR_0									  /*LoRaMac datarates definition, from DR_0 to DR_5*/
#define LORAWAN_TX_POWER TX_POWER_5								  /*LoRaMac tx power definition, from TX_POWER_0 to TX_POWER_15*/
#define JOINREQ_NBTRIALS 3										  /**< Number of trials for the join request. */
DeviceClass_t gCurrentClass = CLASS_A;							  /* class definition*/
lmh_confirm gCurrentConfirm = LMH_CONFIRMED_MSG;				  /* confirm/unconfirm packet definition*/
uint8_t gAppPort = LORAWAN_APP_PORT;							  /* data port*/

/**@brief Structure containing LoRaWan parameters, needed for lmh_init()
 */
static lmh_param_t lora_param_init = {LORAWAN_ADR_ON, LORAWAN_DATERATE, LORAWAN_PUBLIC_NETWORK, JOINREQ_NBTRIALS, LORAWAN_TX_POWER, LORAWAN_DUTYCYCLE_OFF};

// Foward declaration
static void lorawan_has_joined_handler(void);
static void lorawan_rx_handler(lmh_app_data_t *app_data);
static void lorawan_confirm_class_handler(DeviceClass_t Class);
static void send_lora_frame(void);

/**@brief Structure containing LoRaWan callback functions, needed for lmh_init()
*/
static lmh_callback_t lora_callbacks = {BoardGetBatteryLevel, BoardGetUniqueId, BoardGetRandomSeed,
										lorawan_rx_handler, lorawan_has_joined_handler, lorawan_confirm_class_handler};

//OTAA keys !!! KEYS ARE MSB !!!
uint8_t nodeDeviceEUI[8] = {0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x33, 0x33};
uint8_t nodeAppEUI[8] = {0xB8, 0x27, 0xEB, 0xFF, 0xFE, 0x39, 0x00, 0x00};
uint8_t nodeAppKey[16] = {0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x33, 0x33};

// Private defination
#define LORAWAN_APP_DATA_BUFF_SIZE 64										  /**< buffer size of the data to be transmitted. */
#define LORAWAN_APP_INTERVAL 20000											  /**< Defines for user timer, the application data transmission interval. 20s, value in [ms]. */
static uint8_t m_lora_app_data_buffer[LORAWAN_APP_DATA_BUFF_SIZE];			  //< Lora user application data buffer.
static lmh_app_data_t m_lora_app_data = {m_lora_app_data_buffer, 0, 0, 0, 0}; //< Lora user application data structure.

TimerEvent_t appTimer;
static uint32_t timers_init(void);
static uint32_t count = 0;
static uint32_t count_fail = 0;

void setup()
{
	pinMode(LED_BUILTIN, OUTPUT);
	digitalWrite(LED_BUILTIN, LOW);

	// Initialize LoRa chip.
	lora_rak4630_init();

	// Initialize Serial for debug output
	Serial.begin(115200);
	if (!ModbusRTUClient.begin(9600))
	{
		Serial.println("Failed to start Modbus RTU Client!");
		while (1)
			;
	}

	while (!Serial)
	{
		delay(10);
	}
	Serial.println("=====================================");
	Serial.println("Welcome to RAK4630 LoRaWan!!!");
	Serial.println("Type: OTAA");

#if defined(REGION_AS923)
	Serial.println("Region: AS923");
#elif defined(REGION_AU915)
	Serial.println("Region: AU915");
#elif defined(REGION_CN470)
	Serial.println("Region: CN470");
#elif defined(REGION_CN779)
	Serial.println("Region: CN779");
#elif defined(REGION_EU433)
	Serial.println("Region: EU433");
#elif defined(REGION_IN865)
	Serial.println("Region: IN865");
#elif defined(REGION_EU868)
	Serial.println("Region: EU868");
#elif defined(REGION_KR920)
	Serial.println("Region: KR920");
#elif defined(REGION_US915)
	Serial.println("Region: US915");
#elif defined(REGION_US915_HYBRID)
	Serial.println("Region: US915_HYBRID");
#else
	Serial.println("Please define a region in the compiler options.");
#endif
	Serial.println("=====================================");
	Scheduler.startLoop(loop2);
	//creat a user timer to send data to server period
	uint32_t err_code;

	err_code = timers_init();
	if (err_code != 0)
	{
		Serial.printf("timers_init failed - %d\n", err_code);
	}

	// Setup the EUIs and Keys
	lmh_setDevEui(nodeDeviceEUI);
	lmh_setAppEui(nodeAppEUI);
	lmh_setAppKey(nodeAppKey);

	// Initialize LoRaWan
	err_code = lmh_init(&lora_callbacks, lora_param_init, doOTAA);
	if (err_code != 0)
	{
		Serial.printf("lmh_init failed - %d\n", err_code);
	}

	// Start Join procedure
	lmh_join();
}

static unsigned short read_reg(int device_address, int reg_address)
{
	unsigned short reg_value;

	if (!ModbusRTUClient.requestFrom(device_address, HOLDING_REGISTERS, reg_address, 1))
	{
		Serial.print("failed to read registers! ");
		Serial.println(ModbusRTUClient.lastError());
	}
	else
	{
		// If the request succeeds, the sensor sends the readings, that are
		// stored in the holding registers. The read() method can be used to
		// get the raw humidity temperature values.
		reg_value = ModbusRTUClient.read();
	}
	return reg_value;
}

static unsigned short get_soil_conductivity(void)
{
	return read_reg(2, 0x0015);
}

static unsigned short get_soil_temperature(void)
{
	return read_reg(2, 0x0003);
}

static unsigned short get_soil_humidity(void)
{
	return read_reg(2, 0x0002);
}

static unsigned short get_ph(void)
{
	return read_reg(1, 0x0006);
}

static unsigned short get_par(void)
{
	return read_reg(4, 0x0006);
}

static unsigned short get_speed(void)
{
	return read_reg(3, 0x0016);
}

void loop2()
{
	uint32_t i = 0;
	unsigned short par;
	unsigned short raw_conductivity;
	unsigned short raw_temperature;
	unsigned short raw_humidity;
	unsigned short raw_wind_speed;
	unsigned short raw_ph;

	/* RS485 Power on */
	pinMode(34, OUTPUT);
	digitalWrite(34, HIGH);
	delay(300);
	/* RS485 Power on */

	par = get_par();
	raw_conductivity = get_soil_conductivity();
	raw_temperature = get_soil_temperature();
	raw_humidity = get_soil_humidity();
	raw_ph = get_ph();
	raw_wind_speed = get_speed();

	/* RS485 Power off */
	pinMode(34, OUTPUT);
	digitalWrite(34, LOW);
	delay(300);
	/* RS485 Power off */

	Serial.printf("-----par = %d-------\n", par);
	Serial.printf("-----raw_ph = %d-------\n", raw_ph);
	Serial.printf("-----raw_conductivity = %d-------\n", raw_conductivity);
	Serial.printf("-----raw_temperature = %d-------\n", raw_temperature);
	Serial.printf("-----raw_humidity = %d-------\n", raw_humidity);
	Serial.printf("-----raw_wind_speed = %d-------\n", raw_wind_speed);

	m_lora_app_data.port = gAppPort;
	m_lora_app_data.buffer[i++] = 0x0b;
	m_lora_app_data.buffer[i++] = (par >> 8) & 0xFF;
	m_lora_app_data.buffer[i++] = par & 0x00FF;
	m_lora_app_data.buffer[i++] = (raw_ph >> 8) & 0xFF;
	m_lora_app_data.buffer[i++] = raw_ph & 0x00FF;
	m_lora_app_data.buffer[i++] = (raw_conductivity >> 8) & 0xFF;
	m_lora_app_data.buffer[i++] = raw_conductivity & 0x00FF;
	m_lora_app_data.buffer[i++] = (raw_temperature >> 8) & 0xFF;
	m_lora_app_data.buffer[i++] = raw_temperature & 0x00FF;
	m_lora_app_data.buffer[i++] = (raw_humidity >> 8) & 0xFF;
	m_lora_app_data.buffer[i++] = raw_humidity & 0x00FF;
	m_lora_app_data.buffer[i++] = (raw_wind_speed >> 8) & 0xFF;
	m_lora_app_data.buffer[i++] = raw_wind_speed & 0x00FF;
	m_lora_app_data.buffsize = i;

	delay(5000);
}

void loop()
{
	// Handle Radio events
	Radio.IrqProcess();
}

/**@brief LoRa function for handling HasJoined event.
 */
void lorawan_has_joined_handler(void)
{
	Serial.println("OTAA Mode, Network Joined!");

	lmh_error_status ret = lmh_class_request(gCurrentClass);
	if (ret == LMH_SUCCESS)
	{
		//delay(1000);
		TimerSetValue(&appTimer, LORAWAN_APP_INTERVAL);
		TimerStart(&appTimer);
	}
}

/**@brief Function for handling LoRaWan received data from Gateway
 *
 * @param[in] app_data  Pointer to rx data
 */
void lorawan_rx_handler(lmh_app_data_t *app_data)
{
	Serial.printf("LoRa Packet received on port %d, size:%d, rssi:%d, snr:%d, data:%s\n",
				  app_data->port, app_data->buffsize, app_data->rssi, app_data->snr, app_data->buffer);
}

void lorawan_confirm_class_handler(DeviceClass_t Class)
{
	Serial.printf("switch to class %c done\n", "ABC"[Class]);
	// Informs the server that switch has occurred ASAP
	m_lora_app_data.buffsize = 0;
	m_lora_app_data.port = gAppPort;
	lmh_send(&m_lora_app_data, gCurrentConfirm);
}

void send_lora_frame(void)
{
	if (lmh_join_status_get() != LMH_SET)
	{
		//Not joined, try again later
		return;
	}

	lmh_error_status error = lmh_send(&m_lora_app_data, gCurrentConfirm);
	if (error == LMH_SUCCESS)
	{
		count++;
		Serial.printf("lmh_send ok count %d\n", count);
	}
	else
	{
		count_fail++;
		Serial.printf("lmh_send fail count %d\n", count_fail);
	}
	TimerSetValue(&appTimer, LORAWAN_APP_INTERVAL);
	TimerStart(&appTimer);
}

/**@brief Function for handling user timerout event.
 */
void tx_lora_periodic_handler(void)
{
	TimerSetValue(&appTimer, LORAWAN_APP_INTERVAL);
	TimerStart(&appTimer);

	send_lora_frame();
}

/**@brief Function for the Timer initialization.
 *
 * @details Initializes the timer module. This creates and starts application timers.
 */
uint32_t timers_init(void)
{
	TimerInit(&appTimer, tx_lora_periodic_handler);
	return 0;
}
