/*
	Highvoltage Programmer for AVR using ESP8266
	R. J. Tidey 2020/02/29
*/
#include "BaseConfig.h"
#define AP_AUTHID ""
#define	PB5_12V_RST		02	// Control P5 level High off
#define	PB5_VOLTS		05	// P5 voltage High 5V Low 12V
#define	PIN_5V			15	// Control power to AVR LOW off
#define	PB3_HCLK		04	// HV CLOCK
#define	PB2_HDO_CLK		14	// HV DO, Serial CLOCK
#define	PB1_HII_MISO	12	// HV II, Serial MISO
#define	PB0_HDI_MOSI	13	// HV DI, Serial MOSI

byte fuseHigh = 93;
byte fuseLow = 225;   
byte fuseExt = 254; 

#define ERR_NODATAFILE		-1
#define ERR_BADCHKSUM		-2
#define ERR_WAITTIMEOUT		-3

#define FLASH_PAGE_SIZE 64
#define EEPROM_PAGE_SIZE 4
#define PAGE_TOTAL 128
#define MAX_LEN 8192
int iPageSize = 64;
int iPages = 128;
int iSize = 8192;
byte dataBuffer[MAX_LEN]; 
int chkSum; 

void setupStart() {
	digitalWrite(PB5_12V_RST, HIGH);	// PB5 0V
	digitalWrite(PB5_VOLTS, LOW);		// PB5 12V
	digitalWrite(PIN_5V, LOW);			// 5V off
	digitalWrite(PB3_HCLK, LOW);		// Clock low
	pinMode(PIN_5V, OUTPUT);
	pinMode(PB5_12V_RST, OUTPUT);
	pinMode(PB5_VOLTS, OUTPUT);
	pinMode(PB0_HDI_MOSI, OUTPUT);
	pinMode(PB1_HII_MISO, OUTPUT);
	pinMode(PB3_HCLK, OUTPUT);
	pinMode(PB2_HDO_CLK, OUTPUT);
}

// set up lengths for different ATTiny versions
void initSizes(int version, int eeprom) {
	int s;
	Serial.println("Set sizes for " + String(version) + ":" + String(eeprom));
	s = 2 * version + eeprom;
	switch(s) {
		case 0: // ATTiny25 Flash
			iPageSize = 32;
			iPages = 64;
			iSize = 2048;
			break;
		case 1: // ATTiny25 Eeprom
			iPageSize = 4;
			iPages = 32;
			iSize = 128;
			break;
		case 2: // ATTiny45 Flash
			iPageSize = 64;
			iPages = 64;
			iSize = 4096;
			break;
		case 3: // ATTiny45 Eeprom
			iPageSize = 4;
			iPages = 64;
			iSize = 256;
			break;
		case 4 : // ATTiny85 Flash
			iPageSize = 64;
			iPages = 128;
			iSize = 8192;
			break;
		case 5 : // ATTiny85 Eeprom
			iPageSize = 4;
			iPages = 128;
			iSize = 512;
			break;
	}
}
void handleReadFuses() {
	readFuses();
	String status = String(fuseHigh);
	status += ":" + String(fuseLow);
	status += ":" + String(fuseExt);
	server.send(200, "text/html", status);
} 


void handleWriteFuses() {
	String temp1, temp2;
	int val1, val2;
	if (AP_AUTHID != "" && server.arg("auth") != AP_AUTHID) {
		Serial.println("Unauthorized");
		server.send(200, "text/html", "Unauthorized");
	} else {
		temp1 = server.arg("fuseHigh");
		temp2 = server.arg("fuseLow");
		if(temp1.length() && temp2.length()) {
			fuseHigh = temp1.toInt();
			fuseLow = temp2.toInt();
			writeFuses();
			server.send(200, "text/html", "fuses written");
		} else {
			server.send(200, "text/html", "bad arguments");
		}
	}
}

void handleDataOp() {
	String dataFile;
	int ret;
	int dataOp;
	int version;
	int eeprom;
	if (AP_AUTHID != "" && server.arg("auth") != AP_AUTHID) {
		Serial.println("Unauthorized");
		server.send(200, "text/html", "Unauthorized");
	} else {
		version = server.arg("version").toInt();
		dataFile = server.arg("dataFile");
		dataOp = server.arg("dataOp").toInt();
		eeprom = server.arg("eeprom").toInt();
		initSizes(version, eeprom);
		if(dataFile.length()) {
			switch(dataOp) {
				case 0: // read
					ret = hvReadData("/" + dataFile, eeprom);
					break;
				case 1: // write
					ret = hvWriteData("/" + dataFile, eeprom);
					break;
			}
			if(ret >= 0)
				server.send(200, "text/html", "Operation complete");
			else
				server.send(200, "text/html", "Operation error:" + String(ret));
		} else {
			server.send(200, "text/html", "bad argument");
		}
	}
}

void handleEraseChip() {
	if (AP_AUTHID != "" && server.arg("auth") != AP_AUTHID) {
		Serial.println("Unauthorized");
		server.send(200, "text/html", "Unauthorized");
	} else {
		eraseChip();
		server.send(200, "text/html", "Chip erased");
	}
}

void extraHandlers() {
	server.on("/dataOp", handleDataOp);
	server.on("/eraseChip", handleEraseChip);
	server.on("/readFuses", handleReadFuses);
	server.on("/writeFuses", handleWriteFuses);
}
 
void setupEnd() {
}

//return value of a 2 character hex string
unsigned int hex2Int(char c1, char c2) {
	int valH, valL;
	valH = c1 - 48;
	valL = c2 - 48;
	valH -= (valH<10) ? 0 : 7;
	valL -= (valL<10) ? 0 : 7;
	valL += valH<<4;
	chkSum += valL;
	return valL;
}

//Handle a start address by inserting RJMP instruction at 0
void insertStartAddress(int address) {
	int code;
	// make RJMP instruction
	code = ((address >>1) - 1) | 0xC000;
	dataBuffer[0] = code & 0xff;
	dataBuffer[1] = code >> 8;
}

int parseHex(String hexFile) {
	int ret = 0;
	int i;
	int recType;
	int dataIndex;
	int recLength;
	int recAddress;
	int startAddress;
	int chk;
	int eof = 0;
	int format;
	String line;
	//fill flash data with 0xFF
	for(i=0;i<MAX_LEN;i++) dataBuffer[i] = 255;
	File f = SPIFFS.open(hexFile, "r");
	if(f) {
		startAddress = -1;
		while(f.available() && eof == 0) {
			line =f.readStringUntil('\n');
			line.replace("\r","");
			line.replace(" ","");
			if(line.length() > 0) {
				chkSum = 0;
				chk = 0;
				if(line.charAt(0) == ':') {
					format = 1;
					dataIndex = 9;
					recLength = hex2Int(line.charAt(1),line.charAt(2));
					recAddress = (hex2Int(line.charAt(3),line.charAt(4))<<8) + hex2Int(line.charAt(5),line.charAt(6));
					recType = hex2Int(line.charAt(7),line.charAt(8));
					//Serial.println("intel data 0x" + String(recAddress,HEX) + ":" + String(recLength));
				} else {
					format = 0;
					dataIndex = 4;
					recLength = 16;
					recAddress = (hex2Int(line.charAt(0),line.charAt(1))<<8) + hex2Int(line.charAt(2),line.charAt(3));
					recType = 0;
					Serial.println("hex data 0x" + String(recAddress,HEX) + ":" + String(recLength));
				}
				switch(recType) {
					case 0: //data
						for(i=0; i < recLength; i++) {
							dataBuffer[recAddress+i] = hex2Int(line.charAt(dataIndex),line.charAt(dataIndex+1));
							dataIndex +=2;
						}
						chk = 1;
						break;
					case 1: //end of records
						chk = 1;
						eof = 1;
						break;
					case 3: //start address
					case 5: //start address
						startAddress = (hex2Int(line.charAt(13),line.charAt(14))<<8) + hex2Int(line.charAt(15),line.charAt(16));
						chk = 1;
						break;
				}
				if(format == 1 && chk) {
					hex2Int(line.charAt(dataIndex),line.charAt(dataIndex+1));
					if(chkSum & 255 != 0) {
						Serial.println("Bad Intel checkSum");
						ret = ERR_BADCHKSUM;
						eof = 1;
					}
				}	
			}
		}
		f.close();
		if(startAddress > 0) insertStartAddress(startAddress);
	} else {
		Serial.println(String(hexFile) + " not found");
		ret = ERR_NODATAFILE;
	}
	return ret;
}

//return 2 character hex string
String int2Hex(int val) {
	String ret;
	char ch,cl;
	ch = (val >> 4) + 48;
	cl = (val & 0xf) + 48;
	ch += (ch < 58) ? 0 : 7;
	cl += (cl < 58) ? 0 : 7;
	chkSum += val;
	return String(ch) + String(cl);
}

//write buffer as a intel hex file
void writeIntel(String hexFile, int bufferLen) {
	int i,j;
	File f = SPIFFS.open(hexFile, "w");
	for(i = 0; i < bufferLen; i++) {
		if((i & 0xf) == 0) {
			f.print(":10");
			chkSum = 16;
			f.print(int2Hex(i >> 8));
			f.print(int2Hex(i & 0xff));
			f.print(int2Hex(0));
		}
		f.print(int2Hex(dataBuffer[i]));
		if((i & 0xf) == 0xf) {
			f.println(int2Hex((-chkSum) & 0xff));
		}
	}
	//EOF
	f.println(":00000001FF");
	f.close();
}

//functions for reading and writing fuses

//pulse clock. Use 2 writes to slow it down a bit
void pulseClock() {
	digitalWrite(PB3_HCLK, HIGH);
	digitalWrite(PB3_HCLK, HIGH);
  	digitalWrite(PB3_HCLK, LOW);
  	digitalWrite(PB3_HCLK, LOW);
}

//Wait for PB2_HDO_CLK high
int waitSDO() {
	unsigned long t = millis();
	while (!digitalRead(PB2_HDO_CLK)) {
		if(millis() - t > 200) {
			Serial.println("Shift control timeout waiting for SDI");
			return -1;
		}
	}
	return 0;
}

//Shift out data and command byte
int shiftControl(byte val, byte val1, int wait) {
	int i;
	int inData = 0;
	//Start bit
	digitalWrite(PB0_HDI_MOSI, LOW);
	digitalWrite(PB1_HII_MISO, LOW);
	pulseClock();
    
	int mask = 128;    
	for (i = 0; i < 8; i++)  {
		digitalWrite(PB0_HDI_MOSI, (val & mask) ? 1:0);
        digitalWrite(PB1_HII_MISO, (val1 & mask) ? 1:0);
		mask >>=1;
        inData <<=1;
        inData |= digitalRead(PB2_HDO_CLK);
        pulseClock();
	}
	//End bits
	digitalWrite(PB0_HDI_MOSI, LOW);
	digitalWrite(PB1_HII_MISO, LOW);
	pulseClock();
	pulseClock();
	if(wait) {
		if(waitSDO() < 0) 
			return ERR_WAITTIMEOUT;
	}
	delayuSec(10);
	return inData;
}

// Initialize pins to enter high voltage programming mode
void startHV() {
	Serial.println("StartHV" + String(millis()));
    pinMode(PB2_HDO_CLK, OUTPUT);  //Temporary
    digitalWrite(PB0_HDI_MOSI, LOW);
    digitalWrite(PB1_HII_MISO, LOW);
    digitalWrite(PB2_HDO_CLK, LOW);
    digitalWrite(PIN_5V, LOW);  // Power off 5V
    digitalWrite(PB5_12V_RST, HIGH); // Power off 12V
    digitalWrite(PB5_VOLTS, LOW); // Select 12V
    delayMicroseconds(200);
    digitalWrite(PIN_5V, HIGH);  // Apply power to AVR device
    delayMicroseconds(50);
    digitalWrite(PB5_12V_RST, LOW);   //Turn on 12v
    delayMicroseconds(10);
    pinMode(PB2_HDO_CLK, INPUT);   //Release PB2_HDO_CLK
    delayMicroseconds(300);
}

// End high voltage programming mode
void endHV() {
	Serial.println("EndHV" + String(millis()));
    digitalWrite(PB5_12V_RST, HIGH);   //Turn off 12v
    digitalWrite(PIN_5V, LOW);   //Turn off 12v
}

void eraseChip() {
    startHV();
    Serial.println("Erasing chip");
	shiftControl(0x80, 0x4C, 0);
	shiftControl(0x0, 0x64, 0);
	shiftControl(0x0, 0x6C, 1);
	endHV();
}

void hvWrite2Bytes(int buffPtr, int eeprom) {
	if(eeprom == 0) {
		//flash
		shiftControl((buffPtr>>1) & 0xff, 0x0C, 0);
		shiftControl(dataBuffer[buffPtr], 0x2C, 0);
		shiftControl(dataBuffer[buffPtr + 1], 0x3C, 0);
		shiftControl(0, 0x7D, 0);
		shiftControl(0x10, 0x7C, 0);
	} else {
		//eeprom
		int j;
		for(j = buffPtr; j <= (buffPtr + 1); j++) {
			shiftControl(j & 0xff, 0x0C, 0);
			shiftControl((j>>8) & 0xff, 0x1C, 0);
			shiftControl(dataBuffer[j], 0x2C, 0);
			shiftControl(0, 0x6D, 0);
			shiftControl(0, 0x6C, 0);
		}
	}
	delayuSec(20);
}

void hvRead2Bytes(int buffPtr, int eeprom) {
	if(eeprom == 0) {
		//flash
		shiftControl((buffPtr >> 1) & 0xff, 0x0C, 0);
		if(((buffPtr>>1) & 0xff) == 0) {
			shiftControl((buffPtr >> 9), 0x1C, 0);
			ESP.wdtFeed();
			Serial.println("read:" + String(buffPtr,HEX));
		}
		shiftControl(0x00, 0x68, 0);
		dataBuffer[buffPtr] = shiftControl(0x00, 0x6C, 0);
		shiftControl(0x00, 0x78, 0);
		dataBuffer[buffPtr+1] = shiftControl(0x00, 0x7C, 0);
	} else {
		//eeprom
		int j;
		for(j = buffPtr; j <= (buffPtr + 1); j++) {
			shiftControl(j & 0xff, 0x0C, 0);
			if((j & 0xff) == 0) {
				shiftControl((j >> 8), 0x1C, 0);
				ESP.wdtFeed();
				Serial.println("read:" + String(j,HEX));
			}
			shiftControl(0x00, 0x68, 0);
			dataBuffer[j] = shiftControl(0x00, 0x6C, 0);
		}
	}
	delayuSec(20);
}

// write page of data from buffer using high voltage
int hvWriteDataPage(int page, int eeprom) {
	int ret = 0;
	int i;
	int pageOffset;
	int doWrite = 0;
	pageOffset = page * iPageSize;
	//check to see if any programming needed
	for(i = 0; i < iPageSize && doWrite == 0; i +=2) {
		if((dataBuffer[pageOffset + i] != 255) ||  (dataBuffer[pageOffset + i + 1] != 255)) {
			doWrite = 1;
		}
	}
	if(doWrite) {
		Serial.println("Write Page " + String(page));
		//load page buffer
		for(i = 0; i < iPageSize  && ret == 0; i +=2) {
			hvWrite2Bytes(pageOffset + i, eeprom);
		}
		//program buffer
		if(eeprom == 0)	{ //page shifted to create high address bits
			shiftControl(page>>3, 0x1C, 0);
		}
		shiftControl(0, 0x64, 0);
		ret = shiftControl(0, 0x6C, 1);
	} else {
		Serial.println("Skip Page " + String(page));
	}
	return ret;
}


// write pages from buffer using high voltage. flash (eeprom = 0) oe EEPROM eeprom = 1)
int hvWriteDataPages(int eeprom) {
	int ret = 0;
	int page;
	int pageOffset;
	int cmd = eeprom ? 0x11 : 0x10;
    startHV();
    Serial.println("Writing flash:" + String(millis()));
	//writeFlash command
	shiftControl(cmd, 0x4C, 0);
	for(page = 0; page < iPages && ret >= 0; page++) {
		ret = hvWriteDataPage(page, eeprom);
		ESP.wdtFeed();
	}
	//end page programming
	shiftControl(0x00, 0x4C, 0);
    endHV();
	if(ret >=0) ret = 0;
	return ret;
}

// parse and write data. flash (eeprom = 0) oe EEPROM eeprom = 1)
int hvWriteData(String dataFile, int eeprom) {
	int ret;
	Serial.println("Write " + dataFile + " dev=" + String(eeprom));
	if(SPIFFS.exists(dataFile)) {
		ret = parseHex(dataFile);
		if(ret == 0) {
			ret =  hvWriteDataPages(eeprom);
		}
	} else {
		ret = ERR_NODATAFILE;
	}
	return ret;
}

// read data and store in intel hex file
int hvReadData(String dataFile, int eeprom) {
	int ret = 0;
	int i;
    startHV();
    Serial.println("Reading data:");
	//readFlash command
	shiftControl(eeprom ? 0x03 : 0x02, 0x4C, 0);
	for(i = 0; i < iSize;i += 2) {
		hvRead2Bytes(i, eeprom);
	}
    endHV();
	writeIntel(dataFile, iSize);
	return ret;
}

//write a single fuse
int writeFuse(byte fuse, byte ctl1, byte ctl2) {
	int ret = 0;
    shiftControl(0x40, 0x4C, 0);
    shiftControl(fuse, 0x2C, 0);
	shiftControl(0x00, ctl1, 0);
    ret = shiftControl(0x00, ctl2, 1);
	return ret;
}

//write highand low fuses
int writeFuses() {
	int ret;
	Serial.println("write high:" + String(fuseHigh,HEX) + " write low:" + String(fuseLow, HEX));
    startHV();
    //Write fuseHigh
    Serial.println("Writing fuseHigh:" + String(millis()));
	ret = writeFuse(fuseHigh, 0x74, 0x7C);
    
    if(ret >= 0) {
		//Write fuseLow
		Serial.println("Writing fuseLow:" + String(millis()));
		ret = writeFuse(fuseLow, 0x64, 0x6C);
	}
	endHV();
	return ret;
}

//read all 3 fuses
int readFuses(){
	int ret = 0;
	startHV();
     //Read fuseLow
	Serial.println("Reading fuseLow:" + String(millis()));
    shiftControl(0x04, 0x4C, 0);
    shiftControl(0x00, 0x68, 0);
	fuseLow = shiftControl(0x00, 0x6C, 0);
	Serial.println("Reading fuseHigh:" + String(millis()));
	//Read fuseHigh
	shiftControl(0x04, 0x4C, 0);
	shiftControl(0x00, 0x7A, 0);
	fuseHigh = shiftControl(0x00, 0x7E, 0);
	Serial.println("Reading fuseExt:" + String(millis()));
	//Read fuseExt
	shiftControl(0x04, 0x4C, 0);
	shiftControl(0x00, 0x6A, 0);
	fuseExt = shiftControl(0x00, 0x6E, 0);
	endHV();
	Serial.println("fuses H:" + String(fuseHigh,HEX) + " L:" + String(fuseLow,HEX) + " E:" + String(fuseExt,HEX));
	return ret;
}

// loop just services client requests
void loop() {
	server.handleClient();
	wifiConnect(1);
	delaymSec(10);
}
