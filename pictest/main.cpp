#include <cstdio>
/*******************************************************
 PIC reader based on HIDAPI

 George Sun
 A-Concept Inc.

 03/22/2019
********************************************************/

#include <stdio.h>
#include <wchar.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <iostream>
#include "hidapi.h"

using namespace std;

int sendPIC(hid_device *handle, string *cmd);

int main(int argc, char* argv[])
{
	int res;
#define MAX_STR 255
	wchar_t wstr[MAX_STR];
	hid_device *handle = NULL;
	unsigned short VID = 0x04d8;
	unsigned short PID = 0xf2bf;
	unsigned char buf[255];
	string commandQueue = "";
	int i;

	string readSerialNumber = "B20525000100";
	string readGPS = "B2032200";
	string readRadar = "B2032300";
	string readTrigger = "B2032400";
	string readFirmwareVersion = "B204250700";
	string readHardwareVersion = "B20525080100";
	string turnLEDOn = "B20541000100";
	string turnLEDOff = "B20541000000";
	string turnMIC1On = "B2063200020100";
	string turnMIC1Off = "B2063200020000";
	string turnMIC2On = "B2063200010100";
	string turnMIC2Off = "B2063200010100";
	string setHeartbeatOn = "B204F10100";
	string setHeartbeatOff = "B204F10200";
	string getSystemIssueFlags = "B2032700";

	if (hid_init())
		return -1;

	// Open the device using the VID, PID,
	handle = hid_open(VID, PID, NULL);
	if (!handle)
	{
		printf("unable to open device\n");
		return 1;
	}

	// Read the Manufacturer String
	wstr[0] = 0x0000;
	res = hid_get_manufacturer_string(handle, wstr, MAX_STR);
	if (res < 0)
		printf("Unable to read manufacturer string\n");
	else
		printf("\nManufacturer String: %ls\n", wstr);

	// Read the Product String
	wstr[0] = 0x0000;
	res = hid_get_product_string(handle, wstr, MAX_STR);
	if (res < 0)
		printf("Unable to read product string\n");
	else
		printf("Product String: %ls\n", wstr);

	// Read the Serial Number String
	wstr[0] = 0x0000;
	res = hid_get_serial_number_string(handle, wstr, MAX_STR);
	if (res < 0)
		printf("Unable to read serial number string\n");
	else
		printf("Serial Number String: (%d) %ls\n\n", wstr[0], wstr);

	// Set the hid_read() function to be non-blocking.
	hid_set_nonblocking(handle, 1);

	// Read the serial number (cmd 0x25). The first byte is always (0xB2).
	commandQueue = turnMIC1On + readSerialNumber + readHardwareVersion + readFirmwareVersion;

	res = 0;
	int count = 0;
	struct timespec tim = { 0, 990000L }; // sleep for almost 1ms
	struct timeval tv;
	long lastSend = 0;
	long dt;
	string triggers = "";
	string lastTriggers = "";
	memset(buf, 0, sizeof(buf));
	printf("Start dialogue with the PIC.\n");
	printf("The triggers are Pin4-Pin3-Pin6-Pin5 NUL-NUL-LSB-SRN MIC2-MIC1-EMG-RFI LP-CHG-AUX4-HIV TMP-PUR-IMP-IGN");
	while (true)
	{
		// To sleep for 1ms. This may significantly reduce the CPU usage
		clock_nanosleep(CLOCK_REALTIME, 0, &tim, NULL);
		gettimeofday(&tv, NULL);

		if (!handle)
		{
			handle = hid_open(VID, PID, NULL);
			if (!handle)
				continue;

			commandQueue = getSystemIssueFlags;
			hid_set_nonblocking(handle, 1);
		}

		// Try to read from the PIC
		res = hid_read(handle, buf, sizeof(buf));
		if (res < 0)
		{
			printf("\nUnable to read from the PIC.\n");
			printf("Error: %ls\n", hid_error(handle));
			hid_close(handle);
			handle = 0;
			continue;
		}

		// Parse the reading from the PIC
		if (res > 0)
		{
			i = buf[1];
			buf[i] = 0;
			string ID = "Serial Number";
			dt = tv.tv_usec - lastSend;
			if (dt < 0)
				dt += 1000000L;

			switch (buf[2])
			{
			case 0x22: // GPS reading
				printf("\r%ld.%06ld %ld GPS: ", tv.tv_sec, tv.tv_usec, dt);
				for (i = 3; i < buf[1]; i++)
					printf("%c", buf[i]);
				break;

			case 0x25: // Serial number
				if (buf[3] > 47 && buf[3] < 58)
					ID = "Firmware version";
				if (i > 20)
					ID = "Hardware version";
				printf("\n%ld.%06ld %ld %s: %s", tv.tv_sec, tv.tv_usec, dt, ID.c_str(), buf + 3);
				break;

			case 0x24:  // Trigger
				triggers = "";
				for (i = 3; i < buf[1] - 1; i++)
				{
					if (buf[i])
						triggers.append("^");
					else
						triggers.append("_");

					if ((i - 2) % 4 == 0)
						triggers.append(" ");
				}

				if (triggers != lastTriggers)
				{
					printf("\n%ld.%06ld %ld Triggers: %s\n", tv.tv_sec, tv.tv_usec, dt, triggers.c_str());
					lastTriggers = triggers;
				}
				break;

			case 0x41: // LED reading
			case 0x32: // MIC on/off
				break;

			default:
				printf("\n%ld.%06ld %ld : ", tv.tv_sec, tv.tv_usec, dt);
				for (unsigned short i = 0; i < buf[1] + 1; i++)
					printf("%02hx ", buf[i]);
				printf("\n");
			}

			memset(buf, 0, sizeof(buf));
			fflush(stdout);
			continue;
		}

		// Check if it is time to add a periodic command
		count++;
		if (count >= 1000)
			count = 0;  // 1s=1000ms cycle

		switch (count)
		{
		case 0:  // read Triggers every 250ms
		case 250:
		case 500:
		case 750:
			commandQueue.append(readTrigger);
			break;
		case 50:  // turn LED off every second at 50ms
			commandQueue.append(turnLEDOff);
			break;
		case 400:  // read GPS every second at 400ms
			commandQueue.append(readGPS);
			break;
		case 550: // turn LED on every second at 550ms
			commandQueue.append(turnLEDOn);
		}

		// Check if it is safe to send a new command to PIC
		if (commandQueue.length() >= 6)
		{
			lastSend = tv.tv_usec; // sending moment in microseconds
			res = sendPIC(handle, &commandQueue);
			if (res < 0)
			{
				printf("\nUnable to write to the PIC.\n");
				printf("Error: %ls\n", hid_error(handle));
				hid_close(handle);
				handle = 0;
				continue;
			}
		}
		else
			commandQueue.assign("");
	}

	hid_close(handle);

	/* Free static HIDAPI objects. */
	hid_exit();

	return 0;
}

int sendPIC(hid_device *handle, string *cmd)
{
	int len = cmd->length();
	// the command shall be lat least 6 characters
	if (len < 6)
	{
		cmd->assign("");
		return 0;
	}

	unsigned char buf[65];
	unsigned char crc = 0;

	memset(buf, 0, sizeof(buf));
	buf[1] = 0x03; // assign an initial length, required in for loop
	len = len / 2;
	for (int i = 0; i < buf[1] + 1; i++)
	{
		buf[i] = stoi(cmd->substr(i + i, 2), nullptr, 16) & 0xFF;
		crc += buf[i];
	}
	crc = (crc ^ 0xFF) + 1;
	len = buf[1] & 0xFF;
	buf[len] = crc;

	if (len < 3 || len > 60 || buf[0] != 0xB2)
	{
		printf("\nInvalid command %s in queue.\n", cmd->c_str());
		cmd->assign("");
		return -1;
	}

	// Update the command queue
	if (buf[1] + buf[1] < cmd->length())
		cmd->assign(cmd->substr(len + len + 2));
	else
		cmd->assign("");

	return hid_write(handle, buf, len + 1);
};