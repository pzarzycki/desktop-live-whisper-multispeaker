// Copyright (c) 2025 VAM Desktop Live Whisper
// Main entry point for Dear ImGui application (macOS)

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

#include "app_window.hpp"
#include "imgui.h"
#include "imgui_impl_osx.h"
#include "imgui_impl_metal.h"

//-----------------------------------------------------------------------------------
// AppViewController
//-----------------------------------------------------------------------------------

@interface AppViewController : NSViewController
@end

@implementation AppViewController
{
    MTKView* _view;
    id<MTLDevice> _device;
    id<MTLCommandQueue> _commandQueue;
    AppWindow* _appWindow;
}

-(instancetype)initWithNibName:(nullable NSString *)nibNameOrNil bundle:(nullable NSBundle *)nibBundleOrNil
{
    self = [super initWithNibName:nibNameOrNil bundle:nibBundleOrNil];
    
    _device = MTLCreateSystemDefaultDevice();
    _commandQueue = [_device newCommandQueue];
    
    if (!_device)
    {
        NSLog(@"Metal is not supported");
        abort();
    }
    
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    
    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    
    // Load fonts
    // On macOS, use San Francisco system font for native look
    // Fallback to default if not available
    io.Fonts->AddFontDefault();
    
    // Create application window
    _appWindow = new AppWindow();
    
    return self;
}

-(MTKView *)mtkView
{
    return (MTKView *)self.view;
}

-(void)loadView
{
    self.view = [[MTKView alloc] initWithFrame:CGRectMake(0, 0, 1280, 800)];
}

-(void)viewDidLoad
{
    [super viewDidLoad];
    
    self.mtkView.device = _device;
    self.mtkView.delegate = self;
    
    ImGui_ImplOSX_Init(self.view);
    ImGui_ImplMetal_Init(_device);
}

-(void)drawInMTKView:(MTKView*)view
{
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize.x = view.bounds.size.width;
    io.DisplaySize.y = view.bounds.size.height;
    
    CGFloat framebufferScale = view.window.screen.backingScaleFactor ?: NSScreen.mainScreen.backingScaleFactor;
    io.DisplayFramebufferScale = ImVec2(framebufferScale, framebufferScale);
    
    id<MTLCommandBuffer> commandBuffer = [_commandQueue commandBuffer];
    
    MTLRenderPassDescriptor* renderPassDescriptor = view.currentRenderPassDescriptor;
    if (renderPassDescriptor == nil)
    {
        [commandBuffer commit];
        return;
    }
    
    // Start the Dear ImGui frame
    ImGui_ImplMetal_NewFrame(renderPassDescriptor);
    ImGui_ImplOSX_NewFrame(view);
    ImGui::NewFrame();
    
    // Render our application
    _appWindow->Render();
    
    // Check if window should close
    if (_appWindow->ShouldClose())
    {
        [NSApp terminate:nil];
    }
    
    // Rendering
    ImGui::Render();
    ImDrawData* drawData = ImGui::GetDrawData();
    
    renderPassDescriptor.colorAttachments[0].clearColor = MTLClearColorMake(0.12, 0.12, 0.12, 1.0);
    id<MTLRenderCommandEncoder> renderEncoder = [commandBuffer renderCommandEncoderWithDescriptor:renderPassDescriptor];
    ImGui_ImplMetal_RenderDrawData(drawData, commandBuffer, renderEncoder);
    [renderEncoder endEncoding];
    
    [commandBuffer presentDrawable:view.currentDrawable];
    [commandBuffer commit];
}

-(void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size
{
}

-(void)dealloc
{
    delete _appWindow;
    ImGui_ImplMetal_Shutdown();
    ImGui_ImplOSX_Shutdown();
    ImGui::DestroyContext();
}

@end

//-----------------------------------------------------------------------------------
// AppDelegate
//-----------------------------------------------------------------------------------

@interface AppDelegate : NSObject <NSApplicationDelegate>
@property (nonatomic, strong) NSWindow *window;
@end

@implementation AppDelegate

-(BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender
{
    return YES;
}

-(instancetype)init
{
    if (self = [super init])
    {
        // Create the application window
        NSRect frame = NSMakeRect(0, 0, 1280, 800);
        _window = [[NSWindow alloc] initWithContentRect:frame
                                              styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
        _window.title = @"Desktop Live Whisper";
        [_window center];
        
        AppViewController *viewController = [[AppViewController alloc] initWithNibName:nil bundle:nil];
        _window.contentViewController = viewController;
    }
    return self;
}

-(void)applicationDidFinishLaunching:(NSNotification *)notification
{
    [_window makeKeyAndOrderFront:nil];
}

@end

//-----------------------------------------------------------------------------------
// Main
//-----------------------------------------------------------------------------------

int main(int argc, const char * argv[])
{
    @autoreleasepool
    {
        NSApp = [NSApplication sharedApplication];
        AppDelegate *appDelegate = [[AppDelegate alloc] init];
        NSApp.delegate = appDelegate;
        [NSApp run];
    }
    return 0;
}
