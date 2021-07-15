/* Copyright (c) Microsoft Corporation. All rights reserved.
   Licensed under the MIT License. */

#pragma once

/// <summary>
/// Exit codes for this application. These are used for the
/// application exit code.  They must all be between zero and 255,
/// where zero is reserved for successful termination.
/// </summary>
typedef enum {
	DX_ExitCode_Success = 0,
	DX_ExitCode_TermHandler_SigTerm = 254,
	DX_ExitCode_Main_EventLoopFail = 253,
	DX_ExitCode_Missing_ID_Scope = 252,

	DX_ExitCode_Open_Peripheral = 251,
	DX_ExitCode_OpenDeviceTwin = 250,
	DX_ExitCode_AzureCloudToDeviceHandler = 249,
	DX_ExitCode_InterCoreHandler = 248,
	DX_ExitCode_ConsumeEventLoopTimeEvent = 247,
	DX_ExitCode_Gpio_Read = 246,
	DX_ExitCode_InterCoreReceiveFailed = 245,
	DX_ExitCode_PnPModelJsonMemoryAllocationFailed = 244,
	DX_ExitCode_PnPModelJsonFailed = 243,
	DX_ExitCode_IsButtonPressed = 242,
	DX_ExitCode_ButtonPressCheckHandler = 241,
	DX_ExitCode_Led2OffHandler = 240,

	DX_ExitCode_MissingRealTimeComponentId = 239,

	DX_ExitCode_Validate_Hostname_Not_Defined = 238,
    DX_ExitCode_Validate_ScopeId_Not_Defined = 237,
	DX_ExitCode_Validate_Connection_Type_Not_Defined = 236,

	DX_ExitCode_Gpio_Not_Initialized = 235,
	DX_ExitCode_Gpio_Wrong_Direction = 234,
	DX_ExitCode_Gpio_Open_Output_Failed = 233,
	DX_ExitCode_Gpio_Open_Input_Failed = 232,
	DX_ExitCode_Gpio_Open_Direction_Unknown = 231,

	DX_ExitCode_UpdateCallback_UnexpectedEvent = 230,
	DX_ExitCode_UpdateCallback_GetUpdateEvent = 229,
	DX_ExitCode_UpdateCallback_DeferEvent = 228,
	DX_ExitCode_UpdateCallback_FinalUpdate = 227,
	DX_ExitCode_UpdateCallback_UnexpectedStatus = 226,
	DX_ExitCode_SetUpSysEvent_RegisterEvent = 225

} ExitCode;