/*****************************************************************
|
|      Neptune - System Support: Cocoa Implementation
|
|      (c) 2002-2006 Gilles Boccon-Gibod
|      Author: Sylvain Rebaud (sylvain@rebaud.com)
|
****************************************************************/

/*----------------------------------------------------------------------
|       includes
+---------------------------------------------------------------------*/
#include "NptConfig.h"
#include "NptSystem.h"

#if !defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE
#include <Cocoa/Cocoa.h>
#else
#include <UIKit/UIKit.h> 
#endif

#import <SystemConfiguration/SystemConfiguration.h>

NPT_Result
NPT_GetSystemMachineName(NPT_String& name)
{
    // we need a pool because of UTF8String
	@autoreleasepool {
#if !defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE
		NSString *_name = CFBridgingRelease(SCDynamicStoreCopyComputerName(NULL, NULL));
		name = [_name UTF8String];
#else
		name = [[[UIDevice currentDevice] name] UTF8String];
#endif
	}
    return NPT_SUCCESS;
}
