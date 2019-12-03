#include "objectivec.h"
#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#import <CoreData/CoreData.h>

void ObjectiveC::HideWindow()
{
    [NSApp hide:nil];
}
