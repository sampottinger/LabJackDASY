/** Includes **/

// Debug
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

// Windows
#include "stdafx.h"

//	LabJack
#include "c:\program files\labjack\drivers\LabJackUD.h" // TODO: needs to be flexible

// Class header file
#include "LabJackLayer.h"

/**
 * Name: LabJackLayer(DRV_infoStruct *StructAddress, long newDeviceType)
 * Desc: Constructor for LabJackLayer that saves the given DASYLab structure to infoStruct
 * Args: StructAddress, the address of the DASYLab infoStruct
**/
LabJackLayer::LabJackLayer(DRV_INFOSTRUCT * structAddress, long newDeviceType)
{
	// Put in some default values
	aiRetrieveIndex = 0;
	wrapAround = FALSE;
	aoStoreIndex = 0;
	aiStoreIndex = 0;
	analogBufferValid = FALSE;
	open = FALSE;
	measRun = FALSE;

	// Save the structure address and device type
	infoStruct = structAddress;
	deviceType = newDeviceType;

	// Open the LabJack and create buffer
	infoStruct->Error = OpenLabJack (deviceType, LJ_ctUSB, "1", 1, &lngHandle); // U6, UE9
	if (!infoStruct->Error && AllocateAIBuffer (DEFAULT_BUFFER_SIZE)) 
		open = TRUE;
	
	// Fill gains
	// TODO: This is horribly inefficient and inaccurate
	GAIN_INFO[0] = -2;
	GAIN_INFO[1] = 1;
	GAIN_INFO[2] = 2;
	GAIN_INFO[3] = 4;
	GAIN_INFO[4] = 4;
	GAIN_INFO[5] = 4;
	GAIN_INFO[6] = 4;
	GAIN_INFO[7] = 4;
	GAIN_INFO[8] = 4;

	// Fill the information structure
	FillinfoStructure();
}

/**
 * Name: AdvanceAnalogOutputBuf()
 * Desc: Changes aoStoreIndex to the next block for AO output
**/
void LabJackLayer::AdvanceAnalogOutputBuf()
{
	aoCounter += infoStruct->AO_BlockSize;
	aoStoreIndex += infoStruct->AO_BlockSize;

	if ( aoStoreIndex + infoStruct->AO_BlockSize > aoBufferSize )
		aoStoreIndex = 0;
}

/**
 * Name: AdvanceDigitalOutputBuf()
 * Desc: Changes doStoreIndex to the next block for DO output
**/
void LabJackLayer::AdvanceDigitalOutputBuf()
{
	doStoreIndex += infoStruct->DO_BlockSize;

	if ( doStoreIndex + infoStruct->DO_BlockSize > doBufferSize )
		doStoreIndex = 0;
}

/**
 * Name: AdvanceDigitalOutputBuf()
 * Desc: Moves retrieve index one block forward for intermediate buffer
**/
void LabJackLayer::AdvanceAnalogInputBuf()
{
	/* mark processed data - one block processed */
	aiRetrieveIndex += infoStruct->ADI_BlockSize;

	// TODO: This seems dangerous . . . because I wrote it.
	if (aiRetrieveIndex == infoStruct->DriverBufferSize)
	{
		aiRetrieveIndex = 0;
		wrapAround = TRUE;
	}

	if (maxBlocks && !--maxBlocks)
	{
		StopExperiment();
		maxBlocks = (DWORD) -1L;
	}
}

/**
 * Name: GetAnalogOutputBuf()
 * Desc: Returns a pointer to a buffer to new AO data
**/
LPSAMPLE LabJackLayer::GetAnalogOutputBuf()
{
	return aoBufferAdr + aoStoreIndex;
}

/**
 * Name: DRV_GetAnalogOutputStatus()
 * Desc: Test if there is space to place a block of output data
**/
bool LabJackLayer::GetAnalogOutputStatus()
{
	return !(( aoBufferSize == 0 || infoStruct->AO_BlockSize == 0 || aoBufferAdr == NULL )
		&& ( aoCounter + infoStruct->AO_BlockSize > aoBufferSize ));
}

/**
 * Name: GetDigitalOutputBuf()
 * Desc: Returns a pointer to a buffer for new digital output data
**/
LPSAMPLE LabJackLayer::GetDigitalOutputBuf()
{
	return doBufferAdr + doStoreIndex;
}

/**
 * Name: DRV_GetDigitalOutputStatus()
 * Desc: Test if there is space to place a block of output data
**/
bool LabJackLayer::GetDigitalOutputStatus()
{
	if ( doBufferSize == 0 || infoStruct->DO_BlockSize == 0 || doBufferAdr == NULL )
		return FALSE;

	if ( doCounter + infoStruct->DO_BlockSize > doBufferSize )
		return FALSE;

	return TRUE;
}

/**
 * Name: DRV_GetAnalogInputBuf()
 * Desc: Return a pointer to a buffer for new analog input data
**/
LPSAMPLE LabJackLayer::GetAnalogInputBuf()
{
	LPSAMPLE addr;

	if (analogBufferValid)
	{
		/* get adress of data */
		addr = ((LPSAMPLE) aiBufferAdr) + aiRetrieveIndex;
	}
	else
	{
		addr = 0l;
	}

	return addr;
}

/**
 * Name: GetAnalogInputStatus()
 * Desc: Test if input data are waiting to be processed
**/
bool LabJackLayer::GetAnalogInputStatus()
{
	long delta;

	if (maxBlocks == -1L)
		return FALSE;

	delta = aiStoreIndex - aiRetrieveIndex;

	if ( wrapAround )
	{
		delta += infoStruct->DriverBufferSize;
	}

	measInfo.ADI_PercentFull = ( 100L * delta ) / infoStruct->DriverBufferSize;

	analogBufferValid = ( delta >= (long) infoStruct->ADI_BlockSize );

	return analogBufferValid;
}

/**
 * Name: GetMeasInfo()
 * Desc: Return measInfo, the DASYLab structure that holds the status of the experiment
**/
DRV_MEASINFO * LabJackLayer::GetMeasInfo()
{
	return &measInfo;
}

/**
 * Name: IsOpen()
 * Desc: Return TRUE if the device is open and false otherwise
**/
bool LabJackLayer::IsOpen()
{
	return open;
}

/**
 * Name: GetError()
 * Desc: Return the LabJack errorcode of the most recent error encountered or 0
 *		 if no errors have occured
**/
long LabJackLayer::GetError()
{
	return infoStruct->Error;
}

/**
 * Name: FillinfoStructure()
 * Desc: (private) fill the information structure with hardware specific information
**/
void LabJackLayer::FillinfoStructure()
{
	// Frequency and features
	infoStruct->Features = SUPPORT_DEFAULT | SUPPORT_OUT_ALL;
	infoStruct->SupportedAcqModes = DRV_AQM_CONTINUOUS | DRV_AQM_STOP;
	infoStruct->MaxFreq = 50000.0;
	infoStruct->MinFreq = 0.0001;
	infoStruct->MaxFreqPerChan = 50000.0;
	infoStruct->MinFreqPerChan = 0.00001;

	// Device general channel specific settings
	infoStruct->Max_AO_Channel = 2; // DAC0 and DAC1
	infoStruct->Max_CT_Channel = 0; // TODO: Counters have not yet been implemented
	infoStruct->DIO_Width = 1; // 1 bit per channel

	// Make transfer values doubles
	infoStruct->ADI_BlockSize = 8;

	// Channel info?
	//_fmemset (infoStruct->AI_ChInfo, 0, sizeof (infoStruct->AI_ChInfo));
	//_fmemset (infoStruct->DI_ChInfo, 0, sizeof (infoStruct->DI_ChInfo));
	//_fmemset (infoStruct->CT_ChInfo, 0, sizeof (infoStruct->CT_ChInfo));
	//_fmemset (infoStruct->AO_ChInfo, 0, sizeof (infoStruct->AO_ChInfo));
	//_fmemset (infoStruct->DO_ChInfo, 0, sizeof (infoStruct->DO_ChInfo));

	//infoStruct->HelpFileName; // I don't think Vista/7 even supports this!
	//infoStruct->HelpIndex = 0;

	// Fill device model specific information
	switch(deviceType)
	{
		case LJ_dtU3:
			FillU3Info();
			break;
		case LJ_dtU6:
			FillU6Info();
			break;
		case LJ_dtUE9:
			FillUE9Info();
			break;
	}
}

/**
 * Name: FillU3Info()
 * Desc: (private) Fills the DASYLab info structure wtih U3 specific information
**/
void LabJackLayer::FillU3Info()
{
	// TODO: These ought to be constants
	infoStruct->Max_AI_Channel = 19; // LabJackLayer will map 16,17,18 to 30,31,32 respectively
	infoStruct->Max_DI_Channel = 20; // User will need to manage which line is out/in
	infoStruct->Max_DO_Channel = 20;
}

/**
 * Name: FillU6Info()
 * Desc: (private) Fills the DASYLab info structure wtih U6 specific information
**/
void LabJackLayer::FillU6Info()
{
	int n;

	// TODO: These ought to be constants
	infoStruct->Max_AI_Channel = 16;
	infoStruct->Max_DI_Channel = 23; // User will need to manage which line is out/in
	infoStruct->Max_DO_Channel = 23;

	for (n = 0; n < infoStruct->Max_AI_Channel; n++)
	{
		infoStruct->AI_ChInfo[n].InputRange_Max = 10;
		infoStruct->AI_ChInfo[n].InputRange_Min = -10;
		infoStruct->AI_ChInfo[n].Resolution = 65536;	  /* == 16 Bit */
		infoStruct->AI_ChInfo[n].BaseUnit = DRV_BASE_UNIT_2COMP;
	}

	for (n = 0; n < 8; n++)
	{
		infoStruct->GainInfo[n] = GAIN_INFO[n];
	}
}

/**
 * Name: FillUE9Info()
 * Desc: (private) Fills the DASYLab info structure wtih U6 specific information
**/
void LabJackLayer::FillUE9Info()
{
	// TODO: These ought to be constants
	infoStruct->Max_AI_Channel = 22; // LabJackLayer will map 16, 17, 18, 19, 20, 21 to
									 // 128, 132, 133, 136, 140, 141 respectively
	infoStruct->Max_DI_Channel = 23; // User will need to manage which line is out/in
	infoStruct->Max_DO_Channel = 23;
}

/**
 * Name: CleanUp()
 * Desc: Frees up the device and buffers used for DASYLab
**/
void LabJackLayer::CleanUp()
{
	// Clean up the buffers
	//maxRamSize = 0;
	KillBuffer(aiBufferAdr);
	delete aiBufferAdr;
	KillBuffer(aoBufferAdr);
	delete aoBufferAdr;
	KillBuffer(doBufferAdr);
	delete doBufferAdr;

	// Mark the device closed
	open = FALSE;

	// Close through UD driver
	Close();
}

/**
 * Name: KillBuffer(LPSAMPLE & addr)
 * Desc: (private) Cleans up the buffer at the given address
**/
void LabJackLayer::KillBuffer(LPSAMPLE & addr)
{
	if ( addr != NULL && ! measRun )
	{
		FreeLockedMem ( addr );
		addr = NULL;
	}
}

/**
 * Name: AllocateAOBuffer(DWORD nSamples)
 * Desc: Ensures that the analog output buffer for DASYLab is sufficiently large
**/
void LabJackLayer::AllocateAOBuffer(DWORD nSamples)
{
	if ( infoStruct->AO_BlockSize > 1 )
	{
		/* Round to next multiple of AO_BlockSize */
		nSamples += infoStruct->AO_BlockSize / 2;
		nSamples /= infoStruct->AO_BlockSize;
		nSamples *= infoStruct->AO_BlockSize;
	}

	if ( aoBufferSize != nSamples )
	{
		FreeLockedMem ( aoBufferAdr );
		aoBufferAdr = AllocLockedMem ( nSamples, infoStruct );
		aoBufferSize = nSamples;
	}
}

/**
 * Name: SetDigitalOutputBufferMode()
 * Desc: Sets the mode, size, and starting delay for DO and its buffer
**/
void LabJackLayer::SetDigitalOutputBufferMode(DWORD numSamples, DWORD startDelay)
{
	doStartDelay = startDelay;

	if ( infoStruct->DO_BlockSize > 1 )
	{
		// Round to next multiple of DO_BlockSize
		nSamples += infoStruct->DO_BlockSize / 2;
		nSamples /= infoStruct->DO_BlockSize;
		nSamples *= infoStruct->DO_BlockSize;
	}

	if ( doBufferSize != nSamples )
	{
		FreeLockedMem ( doBufferAdr );
		doBufferAdr = AllocLockedMem ( nSamples, infoStruct );
		doBufferSize = nSamples;
	}

}

/**
 * Name: AllocateAOBuffer(DWORD nSamples)
 * Desc: Ensures that the analog input buffer for DASYLab is sufficiently large
 * Note: DASYLab's example checked to see if aiBufferAdr is null after allocating
 *		 memory to see if there is enough and, thus, this method returns bool. 
 *		 It was not present in the others and might not be needed though.
**/
bool LabJackLayer::AllocateAIBuffer(DWORD size)
{
	KillBuffer (aiBufferAdr);

	aiBufferAdr = AllocLockedMem ( size*2, infoStruct );

	// TODO: Didn't need it for the others...
	if ( aiBufferAdr == NULL )
	{
		infoStruct->DriverBufferSize = infoStruct->ADI_BlockSize;
		infoStruct->Error = DRV_ERR_NOTENOUGHMEM;
		return FALSE;
	}

	maxRamSize = size * sizeof (SAMPLE);

	infoStruct->DriverBufferSize = size;
	
	return TRUE;
}

/**
 * Name: IsMeasuring()
 * Desc: Returns true if the device is currently being used in a DASYLab experiment
 *		 or false otherwise
**/
bool LabJackLayer::IsMeasuring()
{
	return measRun;
}

/**
 * Name: SetDeviceType(int type)
 * Desc: Sets the LabJackLayer to looks for a given device type as
 *		 defined in the LabJack header file
**/
void LabJackLayer::SetDeviceType(int type)
{
	deviceType = type;
}

/**
 * Name: BeginExperiment()
 * Desc: Sets up DASYLab information structure and deteremines scan list
 * Args: callbackFunction: the pointer (cast as a long) of the function to have the UD
 *						   driver call. Should be a wrapper to this object's callback
 *						   function.
 **/
void LabJackLayer::BeginExperiment(long callbackFunction)
{
	long lngErrorcode, lngIOType, lngChannel;
	double dblValue;
	int i;

	// Indicate that we have started measuring
	measRun = TRUE;
	
	if (infoStruct->AcquisitionMode == DRV_AQM_STOP)
		maxBlocks = infoStruct->MaxBlocks;
	else
		maxBlocks = 0;

	// (re-)set vars for buffer handling
	wrapAround = FALSE;
	aiRetrieveIndex = 0;
	aiStoreIndex = 0;

	aiChannel = 0;
	aoCount = 0;
	doCount = 0;
	aiCount = 0;

	/* store time */
	startTime = GetCurrentTime ();

	isStreaming = TRUE; // TODO: REALLY need to configure this

	// Find smallest channel
	if (numAINRequested > 0)
	{
		smallestChannel = analogInputScanList[0];
		smallestChannelType = ANALOG;
	}
	else
	{
		smallestChannel = digitalInputScanList[0];
		smallestChannelType = DIGITAL;
	}

	//Configure the stream:
    //Configure all analog inputs for 12-bit resolution
    lngErrorcode = AddRequest(lngHandle, LJ_ioPUT_CONFIG, LJ_chAIN_RESOLUTION, 12, 0, 0);
    ErrorHandler(lngErrorcode);
    //Set the scan rate.
    lngErrorcode = AddRequest(lngHandle, LJ_ioPUT_CONFIG, LJ_chSTREAM_SCAN_FREQUENCY, infoStruct->AI_Frequency/numAINRequested, 0, 0);
    ErrorHandler(lngErrorcode);
    //Give the driver a 5 second buffer (scanRate * channels * 5 seconds).
    lngErrorcode = AddRequest(lngHandle, LJ_ioPUT_CONFIG, LJ_chSTREAM_BUFFER_SIZE, infoStruct->AI_Frequency*numAINRequested*5, 0, 0);
    ErrorHandler(lngErrorcode);
    //Configure reads to retrieve whatever data is available without waiting
    lngErrorcode = AddRequest(lngHandle, LJ_ioPUT_CONFIG, LJ_chSTREAM_WAIT_MODE, LJ_swNONE, 0, 0);
    ErrorHandler(lngErrorcode);
	// Clear stream channels
	lngErrorcode = AddRequest(lngHandle, LJ_ioCLEAR_STREAM_CHANNELS, 0, 0, 0, 0);
    ErrorHandler(lngErrorcode);
    //Define the scan list
	// TODO: Digital Input
	for(i=0; i<numAINRequested; i++)
	{
		lngErrorcode = AddRequest(lngHandle, LJ_ioADD_STREAM_CHANNEL, analogInputScanList[i], 0, 0, 0);
		ErrorHandler(lngErrorcode);
	}

	///* Install MultiMedia interrupt handler */
	//if ( ! InstallTimerInterruptHandler() )
	//{
	//	infoStruct->Error = DRV_ERR_HARD_CONFLICT;
	//	MessageBeep((UINT)-1);
	//	return DRV_FUNCTION_FALSE;
	//}

	//Execute the list of requests.
    lngErrorcode = GoOne(lngHandle);
    ErrorHandler(lngErrorcode);
    
	//Get all the results just to check for errors.
	lngErrorcode = GetFirstResult(lngHandle, &lngIOType, &lngChannel, &dblValue, 0, 0);
	ErrorHandler(lngErrorcode);
	while(lngErrorcode < LJE_MIN_GROUP_ERROR)
	{
		lngErrorcode = GetNextResult(lngHandle, &lngIOType, &lngChannel, &dblValue, 0, 0);
		if(lngErrorcode != LJE_NO_MORE_DATA_AVAILABLE)
			ErrorHandler(lngErrorcode);
	}
    
	// Put in the callback. If the X1 parameter is set to something other than 0
	// the driver will call the specified function after that number of scans
	// have been reached.
	//long pCallback = void (*StreamCallback)(long ScansAvailable, double UserData);
	//pCallback = &StreamCallback;
    lngErrorcode = ePut(lngHandle, LJ_ioSET_STREAM_CALLBACK, callbackFunction, 0, 0);
	ErrorHandler(lngErrorcode);

	//Start the stream.
    lngErrorcode = eGet(lngHandle, LJ_ioSTART_STREAM, 0, &dblValue, 0);
    ErrorHandler(lngErrorcode);
}

/**
 * Name: StopExperiment()
 * Desc: Updates the information structure for DASYLab and stops
 *		 LabJack streaming if applicable
**/
void LabJackLayer::StopExperiment()
{
	long lngErrorcode;

	if(isStreaming)
	{
		//Stop the stream
		lngErrorcode = eGet(lngHandle, LJ_ioSTOP_STREAM, 0, 0, 0);
		ErrorHandler(lngErrorcode);
		isStreaming = FALSE;
	}

	if (measRun)
		measRun = FALSE;

	maxBlocks = 0;
}

/**
 * Name: ConfirmDataStructure()
 * Desc: Verifies the validity of the information strcture for DASYLab
 * Note: mostly copied from TestStruct() in Demo.c from DASYLab
**/
// TODO: This needs some clean up
bool LabJackLayer::ConfirmDataStructure()
{
	DWORD timeBase;
	DWORD minBuffer;
	DWORD blockSize;
	int c, i;						  /* for-loop variables */

	/* initialization complete */
	if (!open)
	{
		infoStruct->Error = DRV_ERR_DEVICENOTINIT;
		return FALSE;
	}
	/* TestStruct nevver called during acquisition */
	if (measRun)
	{
		infoStruct->Error = DRV_ERR_MEASRUN;
		return FALSE;
	}
	/* When TestStruct called, all buffers must be allocated */
	if (aiBufferAdr == NULL)
	{
		infoStruct->Error = DRV_ERR_NOTENOUGHMEM;
		return FALSE;
	}
	/* check if buffersize not to big */
	if (infoStruct->DriverBufferSize * sizeof (SAMPLE) != maxRamSize)
	{
		infoStruct->Error = DRV_ERR_BUFSIZETOBIG;
		return FALSE;
	}
	/* check if any channel is active */
	if (numAINRequested == 0 && numDIRequested == 0)
	{
		infoStruct->Error = DRV_ERR_NOCHANNEL;
		return FALSE;
	}
	/* check rate-parameters */
	if (infoStruct->AI_Frequency > infoStruct->MaxFreq)
	{
		infoStruct->AI_Frequency = infoStruct->MaxFreq;
		infoStruct->Error = DRV_WARN_CHANGEFREQ;
		return FALSE;
	}
	if (infoStruct->AI_Frequency < infoStruct->MinFreq)
	{
		infoStruct->AI_Frequency = infoStruct->MinFreq;
		infoStruct->Error = DRV_WARN_CHANGEFREQ;
		return FALSE;
	}

	/* Round Frequency to nearest possible value */
	//timeBase = 100;	 /* Use 100 Hz MultiMedia interrupt as time base */

	//PacerRate = (DWORD) ( 0.5 + ( TimeBase / infoStruct->AI_Frequency ) );
	//if ( PacerRate < 1 )
	//	PacerRate = 1;

	//infoStruct->AI_Frequency = TimeBase / (double) PacerRate;

	/* Check for correct rates */
	if (infoStruct->DI_FreqRate <= 0)
		infoStruct->DI_FreqRate = 1;
	if (infoStruct->AO_FreqRate <= 0)
		infoStruct->AO_FreqRate = 1;
	if (infoStruct->DO_FreqRate <= 0)
		infoStruct->DO_FreqRate = 1;


	/* be sure, that the whole bufferlength is */
	/* a multiple of the blocksize								 */

	if (infoStruct->DriverBufferSize * sizeof (SAMPLE) != maxRamSize)
		AllocateAIBuffer (infoStruct->DriverBufferSize);
	blockSize = infoStruct->ADI_BlockSize * sizeof (SAMPLE);	  /* Recalculate from SAMPLES to BYTES */
	minBuffer = blockSize * (maxRamSize / blockSize);
	if (minBuffer != maxRamSize)
	{
		AllocateAIBuffer (minBuffer / sizeof (SAMPLE));
	}

	aiStoreIndex = 0l;
	//maxBufferIndex = maxRamSize / sizeof (SAMPLE);

	/* calculate count of active channels */
	// TODO: Variable 
	//AnzahlChannel = (WORD) (Num_AI_Channels ());
	//OutputChannel = (BOOL) (infoStruct->AO_Channel != (DWORD) 0);
	//DigitalInput = (BOOL) (infoStruct->DI_Channel != (DWORD) 0);
	//OutputDigital = (BOOL) (infoStruct->DO_Channel != (DWORD) 0);
	//Counter = (BOOL) (infoStruct->CT_Channel != (DWORD) 0);

	/* check and store which channels to poll in local array (access-time) */
	// TODO: Need constants!
	for (c = 0, i = 0; i < 32; i++)
	{
		if (IsRequestingAIN(i))
		{
			analogInputScanList[c] = i;
			c++;
		}
	}
	numAINRequested = c;

	for (c = 0, i = 0; i < 32; i++)
	{
		if (IsRequestingDI(i))
		{
			digitalInputScanList[c] = i;
			c++;
		}
	}
	numDIRequested = c;

	/* calculate sizes in sample */
	//blockSizeInSamples = infoStruct->ADI_BlockSize;
	//bufferSizeInSamples = infoStruct->DriverBufferSize;

	//freqInMS = (DWORD) (1000.0 / (infoStruct->AI_Frequency / BlockSize));

	return TRUE;
}

/**
 * Name: StreamCallback(long scansAvailable, double userValue)
 * Desc: Stream callback function that places values read by LabJack 
 *		 into DASYLab buffer
**/
void LabJackLayer::StreamCallback(long scansAvailable, double userValue)
{
	
	long lngErrorcode;
	int i;
	double dblScansAvailable = scansAvailable;
	double adblData[4000]; // TODO: Dynamic allocation
	long padblData = (long)&adblData[0];

	UNUSED(userValue);

	lngErrorcode = eGet(lngHandle, LJ_ioGET_STREAM_DATA, LJ_chALL_CHANNELS, &dblScansAvailable, padblData);
	ErrorHandler(lngErrorcode);
	
	// Convert values and place into buffer
	for(i=0; i<numAINRequested; i++)
	{
		/* feed channel value in FIFO */
		aiBufferAdr[aiStoreIndex] = ConvertAIValue(adblData[i], i);

		/* increment FIFO index and wrap around */
		aiStoreIndex++;
		if (aiStoreIndex == infoStruct->DriverBufferSize)
		{
			aiStoreIndex = 0;
			wrapAround = TRUE;
		}

		aiCount++;
	}
}

/**
 * Name: ErrorHandler(LJ_ERROR lngErrorcode)
 * Desc: Checks for errors after every LabJack UD call
**/
void LabJackLayer::ErrorHandler (LJ_ERROR lngErrorcode)
{
	if (lngErrorcode != LJE_NOERROR)
	{
		infoStruct->Error = lngErrorcode;
		DRV_ShowError();
	}
}

/**
 * Name: IsRequestingAIN(int channel)
 * Desc: Returns True if the user is polling/streaming the given AIN
		 channel or False otherwise.
**/
bool LabJackLayer::IsRequestingAIN(int channel)
{
	return (infoStruct->AI_Channel[0] & (1 << channel)) > 0;
}

/**
 * Name: IsRequestingDI(int channel)
 * Desc: Returns True if the user is polling/streaming the given digital
 *		 channel as input or False otherwise.
**/
bool LabJackLayer::IsRequestingDI(int channel)
{
	return (infoStruct->DI_Channel & (1 << channel)) > 0;
}

/**
 * Name: ConvertAIValue
 * Desc: Converts a normal double into a value
 *		 suitable for DASYLab AIN use.
**/
SAMPLE LabJackLayer::ConvertAIValue(double value, UINT channel)
{
	// TODO: This is a really unacceptable excuse for shuffling data around
	// Calculate range
	double inputRange = infoStruct->AI_ChInfo[channel].InputRange_Max - infoStruct->AI_ChInfo[channel].InputRange_Min;
	double bitsAvailable = sizeof(SAMPLE) * 8;
	double maxValue = pow(2.0, bitsAvailable-1);
	double conversionFactor = (maxValue / inputRange); // Multiply by 2 to get unsigned max
	return (SAMPLE) (conversionFactor * value);
}

/**
 * Name: FreeLockedMem
 * Desc: Unlocks memory taken by DASYLab
 * Note: This is from the DASYLab example and left mostly unchanged
**/
void LabJackLayer::FreeLockedMem (LPSAMPLE bufferadr)
{
	HGLOBAL hMem;

	if ( bufferadr == NULL )
		return;

	hMem = GlobalPtrHandle(bufferadr);

	if ( hMem == 0 )
		return;

#ifdef WIN32
	VirtualUnlock(hMem,GlobalSize(hMem));
	GlobalUnlock (hMem);
#else
	GlobalUnlock (hMem);
	GlobalPageUnlock (hMem);
	GlobalUnfix (hMem);
#endif

	GlobalFree (hMem);
}

/**
 * Name: AllocLockedMem (DWORD nSamples)
 * Desc: Allocatese reserved memory for DASYLab use
 * Note: This is simply taken from the DASYLab driver example
**/
LPSAMPLE LabJackLayer::AllocLockedMem (DWORD nSamples, DRV_INFOSTRUCT * infoStruct)
{
	LPSAMPLE bufferadr;
	HGLOBAL hMem;

	if ( nSamples == 0 )
		return NULL;

	GlobalCompact (nSamples * 8);// TODO: infoStruct->ADI_BlockSize);
	hMem = GlobalAlloc (GMEM_FIXED | GMEM_ZEROINIT, nSamples * sizeof(SAMPLE));
	if (!hMem)
	{
		infoStruct->Error = DRV_ERR_NOTENOUGHMEM;
		return NULL;
	}
#ifdef WIN32
	bufferadr = (LPSAMPLE) GlobalLock (hMem);
	VirtualLock(bufferadr,nSamples * sizeof(SAMPLE));
#else
	GlobalFix (hMem);
	GlobalPageLock (hMem);
	bufferadr = (LPSAMPLE) GlobalLock (hMem);
#endif

	return bufferadr;
	/*SAMPLE * bufferAdr = NULL;
	bufferAdr = new SAMPLE[nSamples];
	return bufferAdr;*/
}

/**
 * Name: SetError(DWORD newError)
 * Desc: Allows client to set the error state of DASYLab's
 *		 InfoStruct directly.
 * Note: This borders on a bad practice and should be avoided
**/
void LabJackLayer::SetError(DWORD newError)
{
	infoStruct->Error = newError;
}