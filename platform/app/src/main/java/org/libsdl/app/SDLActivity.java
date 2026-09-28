package org.libsdl.app;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.Dialog;
import android.app.UiModeManager;
import android.content.ActivityNotFoundException;
import android.content.ClipboardManager;
import android.content.ClipData;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.content.res.Configuration;
import android.graphics.Bitmap;
import android.graphics.Color;
import android.graphics.PorterDuff;
import android.graphics.drawable.Drawable;
import android.hardware.Sensor;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.LocaleList;
import android.os.Looper;
import android.os.Message;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.provider.Settings;
import android.text.Editable;
import android.text.InputType;
import android.text.Selection;
import android.util.DisplayMetrics;
import android.util.Log;
import android.util.SparseArray;
import android.view.Display;
import android.view.Gravity;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.PointerIcon;
import android.view.Surface;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;
import android.webkit.MimeTypeMap;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.RelativeLayout;
import android.widget.TextView;
import android.widget.Toast;
import android.window.OnBackInvokedCallback;
import android.window.OnBackInvokedDispatcher;

import org.jetbrains.annotations.Nullable;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.Hashtable;
import java.util.Locale;


/**
    SDL Activity
*/
public class SDLActivity extends Activity implements View.OnSystemUiVisibilityChangeListener {
    private static final String TAG = "SDL";
    private static final int SDL_MAJOR_VERSION = 3;
    private static final int SDL_MINOR_VERSION = 5;
    private static final int SDL_MICRO_VERSION = 0;

    public static boolean mIsResumedCalled, mHasFocus;
    public static final boolean mHasMultiWindow = (Build.VERSION.SDK_INT >= 24  /* Android 7.0 (N) */);

    // Cursor types
    private static final int SDL_SYSTEM_CURSOR_DEFAULT = 0;
    private static final int SDL_SYSTEM_CURSOR_TEXT = 1;
    private static final int SDL_SYSTEM_CURSOR_WAIT = 2;
    private static final int SDL_SYSTEM_CURSOR_CROSSHAIR = 3;
    private static final int SDL_SYSTEM_CURSOR_PROGRESS = 4;
    private static final int SDL_SYSTEM_CURSOR_NWSE_RESIZE = 5;
    private static final int SDL_SYSTEM_CURSOR_NESW_RESIZE = 6;
    private static final int SDL_SYSTEM_CURSOR_EW_RESIZE = 7;
    private static final int SDL_SYSTEM_CURSOR_NS_RESIZE = 8;
    private static final int SDL_SYSTEM_CURSOR_MOVE = 9;
    private static final int SDL_SYSTEM_CURSOR_NOT_ALLOWED = 10;
    private static final int SDL_SYSTEM_CURSOR_POINTER = 11;
    private static final int SDL_SYSTEM_CURSOR_NW_RESIZE = 12;
    private static final int SDL_SYSTEM_CURSOR_N_RESIZE = 13;
    private static final int SDL_SYSTEM_CURSOR_NE_RESIZE = 14;
    private static final int SDL_SYSTEM_CURSOR_E_RESIZE = 15;
    private static final int SDL_SYSTEM_CURSOR_SE_RESIZE = 16;
    private static final int SDL_SYSTEM_CURSOR_S_RESIZE = 17;
    private static final int SDL_SYSTEM_CURSOR_SW_RESIZE = 18;
    private static final int SDL_SYSTEM_CURSOR_W_RESIZE = 19;
    private static final int SDL_SYSTEM_CURSOR_CONTEXT_MENU = 20;
    private static final int SDL_SYSTEM_CURSOR_HELP = 21;
    private static final int SDL_SYSTEM_CURSOR_CELL = 22;
    private static final int SDL_SYSTEM_CURSOR_VERTICAL_TEXT = 23;
    private static final int SDL_SYSTEM_CURSOR_ALIAS = 24;
    private static final int SDL_SYSTEM_CURSOR_COPY = 25;
    private static final int SDL_SYSTEM_CURSOR_NO_DROP = 26;
    private static final int SDL_SYSTEM_CURSOR_GRAB = 27;
    private static final int SDL_SYSTEM_CURSOR_GRABBING = 28;
    private static final int SDL_SYSTEM_CURSOR_COL_RESIZE = 29;
    private static final int SDL_SYSTEM_CURSOR_ROW_RESIZE = 30;
    private static final int SDL_SYSTEM_CURSOR_ALL_SCROLL = 31;
    private static final int SDL_SYSTEM_CURSOR_ZOOM_IN = 32;
    private static final int SDL_SYSTEM_CURSOR_ZOOM_OUT = 33;

    protected static final int SDL_ORIENTATION_UNKNOWN = 0;
    protected static final int SDL_ORIENTATION_LANDSCAPE = 1;
    protected static final int SDL_ORIENTATION_LANDSCAPE_FLIPPED = 2;
    protected static final int SDL_ORIENTATION_PORTRAIT = 3;
    protected static final int SDL_ORIENTATION_PORTRAIT_FLIPPED = 4;

    protected static int mCurrentRotation;
    protected static Locale mCurrentLocale;

    // Handle the state of the native layer
    public enum NativeState {
           INIT, RESUMED, PAUSED
    }

    public static NativeState mNextNativeState;
    public static NativeState mCurrentNativeState;

    /** If shared libraries (e.g. SDL or the native application) could not be loaded. */
    public static boolean mBrokenLibraries = true;

    // Main components
    protected static SDLActivity mSingleton;
    protected static SDLSurface mSurface;
    protected static SDLDummyEdit mTextEdit;
    protected static ViewGroup mLayout;
    protected static SDLClipboardHandler mClipboardHandler;
    protected static Hashtable<Integer, PointerIcon> mCursors;
    protected static int mLastCursorID;
    protected static SDLGenericMotionListener_API14 mMotionListener;
    protected static HIDDeviceManager mHIDDeviceManager;

    // This is what SDL runs in. It invokes SDL_main(), eventually
    protected static Thread mSDLThread;
    protected static boolean mSDLMainFinished = false;
    protected static boolean mActivityCreated = false;
    private static SDLFileDialogState mFileDialogState = null;
    protected static boolean mDispatchingKeyEvent = false;

    public static SDLGenericMotionListener_API14 getMotionListener() {
        if (mMotionListener == null) {
            if (Build.VERSION.SDK_INT >= 29 /* Android 10 (Q) */) {
                mMotionListener = new SDLGenericMotionListener_API29();
            } else if (Build.VERSION.SDK_INT >= 26 /* Android 8.0 (O) */) {
                mMotionListener = new SDLGenericMotionListener_API26();
            } else if (Build.VERSION.SDK_INT >= 24 /* Android 7.0 (N) */) {
                mMotionListener = new SDLGenericMotionListener_API24();
            } else {
                mMotionListener = new SDLGenericMotionListener_API14();
            }
        }

        return mMotionListener;
    }

    protected void main() {
        String library = SDLActivity.mSingleton.getMainSharedObject();
        String function = SDLActivity.mSingleton.getMainFunction();
        String[] arguments = SDLActivity.mSingleton.getArguments();

        Log.v("SDL", "Running main function " + function + " from library " + library);
        SDLActivity.nativeRunMain(library, function, arguments);
        Log.v("SDL", "Finished main function");
    }

    protected String getMainSharedObject() {
        String library;
        String[] libraries = SDLActivity.mSingleton.getLibraries();
        if (libraries.length > 0) {
            library = "lib" + libraries[libraries.length - 1] + ".so";
        } else {
            library = "libmain.so";
        }
        return getContext().getApplicationInfo().nativeLibraryDir + "/" + library;
    }

    protected String getMainFunction() {
        return "SDL_main";
    }

    protected String[] getLibraries() {
        return new String[] {
            "curl",
            // "SDL3_image",
            // "SDL3_mixer",
            // "SDL3_net",
            // "SDL3_ttf",
            "main"
        };
    }

    // Load the .so
    public void loadLibraries() {
       for (String lib : getLibraries()) {
          SDL.loadLibrary(lib, this);
       }
    }

    protected String[] getArguments() {
        return new String[0];
    }

    protected int getInitSubsystems() {
        return SDL.SDL_INIT_EVERYTHING;
    }

    public static void initialize() {
        mSingleton = null;
        mSurface = null;
        mTextEdit = null;
        mLayout = null;
        mClipboardHandler = null;
        mCursors = new Hashtable<Integer, PointerIcon>();
        mLastCursorID = 0;
        mSDLThread = null;
        mIsResumedCalled = false;
        mHasFocus = true;
        mNextNativeState = NativeState.INIT;
        mCurrentNativeState = NativeState.INIT;
    }

    protected SDLSurface createSDLSurface(Context context) {
        return new SDLSurface(context);
    }

    // Custom file picker invocation
    public void openFilePicker() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        startActivityForResult(intent, 1001);
    }

    // Setup
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        Log.v(TAG, "Manufacturer: " + Build.MANUFACTURER);
        Log.v(TAG, "Device: " + Build.DEVICE);
        Log.v(TAG, "Model: " + Build.MODEL);
        Log.v(TAG, "onCreate()");
        super.onCreate(savedInstanceState);

        if (Build.VERSION.SDK_INT >= 30 /* Android 11 (R) */) {
            getWindow().setDecorFitsSystemWindows(false);
        }

        /* Control activity re-creation */
        if (mSDLMainFinished || mActivityCreated) {
              boolean allow_recreate = SDLActivity.nativeAllowRecreateActivity();
              if (mSDLMainFinished) {
                  Log.v(TAG, "SDL main() finished");
              }
              if (allow_recreate) {
                  Log.v(TAG, "activity re-created");
              } else {
                  Log.v(TAG, "activity finished");
                  System.exit(0);
                  return;
              }
        }

        mActivityCreated = true;

        try {
            Thread.currentThread().setName("SDLActivity");
        } catch (Exception e) {
            Log.v(TAG, "modify thread properties failed " + e.toString());
        }

        // Load shared libraries
        String errorMsgBrokenLib = "";
        try {
            loadLibraries();
            mBrokenLibraries = false; /* success */
        } catch(UnsatisfiedLinkError e) {
            System.err.println(e.getMessage());
            mBrokenLibraries = true;
            errorMsgBrokenLib = e.getMessage();
        } catch(Exception e) {
            System.err.println(e.getMessage());
            mBrokenLibraries = true;
            errorMsgBrokenLib = e.getMessage();
        }

        if (!mBrokenLibraries) {
            String expected_version = String.valueOf(SDL_MAJOR_VERSION) + "." +
                                      String.valueOf(SDL_MINOR_VERSION) + "." +
                                      String.valueOf(SDL_MICRO_VERSION);
            String version = nativeGetVersion();
            if (!version.equals(expected_version)) {
                mBrokenLibraries = true;
                errorMsgBrokenLib = "SDL C/Java version mismatch (expected " + expected_version + ", got " + version + ")";
            }
        }

        if (mBrokenLibraries) {
            mSingleton = this;
            AlertDialog.Builder dlgAlert  = new AlertDialog.Builder(this);
            dlgAlert.setMessage("An error occurred while trying to start the application. Please try again and/or reinstall."
                  + System.getProperty("line.separator")
                  + System.getProperty("line.separator")
                  + "Error: " + errorMsgBrokenLib);
            dlgAlert.setTitle("SDL Error");
            dlgAlert.setPositiveButton("Exit",
                new DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(DialogInterface dialog,int id) {
                        SDLActivity.mSingleton.finish();
                    }
                });
           dlgAlert.setCancelable(false);
           dlgAlert.create().show();

           return;
        }

        /* Control activity re-creation */
        {
            int run_count = SDLActivity.nativeCheckSDLThreadCounter();
            if (run_count != 0) {
                boolean allow_recreate = SDLActivity.nativeAllowRecreateActivity();
                if (allow_recreate) {
                    Log.v(TAG, "activity re-created // run_count: " + run_count);
                } else {
                    Log.v(TAG, "activity finished // run_count: " + run_count);
                    System.exit(0);
                    return;
                }
            }
        }

        // Set up JNI
        SDL.setupJNI(getInitSubsystems());

        // Initialize state
        SDL.initialize();

        // So we can call stuff from static callbacks
        mSingleton = this;
        SDL.setContext(this);

        if (SDL.isControllerManagerReady()) {
            SDLControllerManager.initializeDeviceListener();
        }

        if (SDL.isSubsystemCompiled(SDL.SDL_INIT_VIDEO)) {
            mClipboardHandler = new SDLClipboardHandler();
        }

        if (nativeIsHIDAPIEnabled()) {
            mHIDDeviceManager = HIDDeviceManager.acquire(this);
        }

        // Set up the surface
        if (SDL.isSubsystemInitialized(SDL.SDL_INIT_VIDEO)) {
            mSurface = createSDLSurface(this);

            mLayout = new RelativeLayout(this);
            mLayout.addView(mSurface);

            // Get our current screen orientation and pass it down.
            SDLActivity.nativeSetNaturalOrientation(SDLActivity.getNaturalOrientation());
            mCurrentRotation = SDLActivity.getCurrentRotation();
            SDLActivity.onNativeRotationChanged(mCurrentRotation);
        }

        try {
            if (Build.VERSION.SDK_INT < 24 /* Android 7.0 (N) */) {
                mCurrentLocale = getContext().getResources().getConfiguration().locale;
            } else {
                mCurrentLocale = getContext().getResources().getConfiguration().getLocales().get(0);
            }
        } catch(Exception ignored) {
        }

        switch (getContext().getResources().getConfiguration().uiMode & Configuration.UI_MODE_NIGHT_MASK) {
        case Configuration.UI_MODE_NIGHT_NO:
            SDLActivity.onNativeDarkModeChanged(false);
            break;
        case Configuration.UI_MODE_NIGHT_YES:
            SDLActivity.onNativeDarkModeChanged(true);
            break;
        }

        if (mLayout != null) {
            setContentView(mLayout);
            setWindowStyle(false);
        }

        getWindow().getDecorView().setOnSystemUiVisibilityChangeListener(this);

        // Get filename from "Open with" of another application
        if (SDL.isSubsystemInitialized(SDL.SDL_INIT_VIDEO)) {
            Intent intent = getIntent();
            if (intent != null && intent.getData() != null) {
                String filename = intent.getData().getPath();
                if (filename != null) {
                    Log.v(TAG, "Got filename: " + filename);
                    SDLActivity.onNativeDropFile(filename);
                }
            }
        }
    }

    protected void pauseNativeThread() {
        mNextNativeState = NativeState.PAUSED;
        mIsResumedCalled = false;

        if (SDLActivity.mBrokenLibraries) {
            return;
        }

        SDLActivity.handleNativeState();
    }

    protected void resumeNativeThread() {
        mNextNativeState = NativeState.RESUMED;
        mIsResumedCalled = true;

        if (SDLActivity.mBrokenLibraries) {
           return;
        }

        SDLActivity.handleNativeState();
    }

    // Events
    @Override
    protected void onPause() {
        Log.v(TAG, "onPause()");
        super.onPause();

        if (mHIDDeviceManager != null) {
            mHIDDeviceManager.setFrozen(true);
        }

        if (!mHasMultiWindow) {
            pauseNativeThread();
        }
    }

    @Override
    protected void onResume() {
        Log.v(TAG, "onResume()");
        super.onResume();

        if (mHIDDeviceManager != null) {
            mHIDDeviceManager.setFrozen(false);
        }

        if (!mHasMultiWindow) {
            resumeNativeThread();
        }
    }

    @Override
    protected void onStop() {
        Log.v(TAG, "onStop()");
        super.onStop();
        if (mHasMultiWindow) {
            pauseNativeThread();
        }
    }

    @Override
    protected void onStart() {
        Log.v(TAG, "onStart()");
        super.onStart();
        if (mHasMultiWindow) {
            resumeNativeThread();
        }
    }

    public static int getNaturalOrientation() {
        int result = SDL_ORIENTATION_UNKNOWN;

        Activity activity = getContext();
        if (activity != null) {
            Configuration config = activity.getResources().getConfiguration();
            Display display = activity.getWindowManager().getDefaultDisplay();
            int rotation = display.getRotation();
            if (((rotation == Surface.ROTATION_0 || rotation == Surface.ROTATION_180) &&
                    config.orientation == Configuration.ORIENTATION_LANDSCAPE) ||
                ((rotation == Surface.ROTATION_90 || rotation == Surface.ROTATION_270) &&
                    config.orientation == Configuration.ORIENTATION_PORTRAIT)) {
                result = SDL_ORIENTATION_LANDSCAPE;
            } else {
                result = SDL_ORIENTATION_PORTRAIT;
            }
        }
        return result;
    }

    public static int getCurrentRotation() {
        int result = 0;

        Activity activity = getContext();
        if (activity != null) {
            Display display = activity.getWindowManager().getDefaultDisplay();
            switch (display.getRotation()) {
                case Surface.ROTATION_0:
                    result = 0;
                    break;
                case Surface.ROTATION_90:
                    result = 90;
                    break;
                case Surface.ROTATION_180:
                    result = 180;
                    break;
                case Surface.ROTATION_270:
                    result = 270;
                    break;
            }
        }
        return result;
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        Log.v(TAG, "onWindowFocusChanged(): " + hasFocus);

        if (hasFocus || !SDLActivity.nativeGetHintBoolean("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", false)) {
            if (mHIDDeviceManager != null) {
                mHIDDeviceManager.setFrozen(!hasFocus);
            }
        }

        if (SDLActivity.mBrokenLibraries) {
           return;
        }

        mHasFocus = hasFocus;
        if (hasFocus) {
           mNextNativeState = NativeState.RESUMED;
           SDLActivity.getMotionListener().reclaimRelativeMouseModeIfNeeded();

           SDLActivity.handleNativeState();
           nativeFocusChanged(true);

        } else {
           nativeFocusChanged(false);
           if (!mHasMultiWindow) {
               mNextNativeState = NativeState.PAUSED;
               SDLActivity.handleNativeState();
           }
        }
    }

    @Override
    public void onTrimMemory(int level) {
        Log.v(TAG, "onTrimMemory()");
        super.onTrimMemory(level);

        if (SDLActivity.mBrokenLibraries) {
           return;
        }

        SDLActivity.nativeLowMemory();
    }

    @Override
    public void onConfigurationChanged(Configuration newConfig) {
        Log.v(TAG, "onConfigurationChanged()");
        super.onConfigurationChanged(newConfig);

        if (SDLActivity.mBrokenLibraries) {
           return;
        }

        if (mCurrentLocale == null || !mCurrentLocale.equals(newConfig.locale)) {
            mCurrentLocale = newConfig.locale;
            SDLActivity.onNativeLocaleChanged();
        }

        switch (newConfig.uiMode & Configuration.UI_MODE_NIGHT_MASK) {
        case Configuration.UI_MODE_NIGHT_NO:
            SDLActivity.onNativeDarkModeChanged(false);
            break;
        case Configuration.UI_MODE_NIGHT_YES:
            SDLActivity.onNativeDarkModeChanged(true);
            break;
        }
    }

    @Override
    protected void onDestroy() {
        Log.v(TAG, "onDestroy()");

        if (mHIDDeviceManager != null) {
            HIDDeviceManager.release(mHIDDeviceManager);
            mHIDDeviceManager = null;
        }

        SDLAudioManager.release(this);

        if (SDLActivity.mBrokenLibraries) {
           super.onDestroy();
           return;
        }

        if (SDLActivity.mSDLThread != null) {
            SDLActivity.nativeSendQuit();

            try {
                SDLActivity.mSDLThread.join(1000);
            } catch(Exception e) {
                Log.v(TAG, "Problem stopping SDLThread: " + e);
            }
        }

        SDLActivity.nativeQuit();

        super.onDestroy();
    }

    @Override
    public void onBackPressed() {
        boolean trapBack = SDLActivity.nativeGetHintBoolean("SDL_ANDROID_TRAP_BACK_BUTTON", false);
        if (trapBack) {
            return;
        }

        if (!isFinishing()) {
            super.onBackPressed();
        }
    }

    static OnBackInvokedCallback backButtonCallback;
    static boolean mBackKeyTrapEnabled = false;
    public static void setBackButtonTrapEnabled(boolean enabled) {

        if ( Build.VERSION.SDK_INT < 33 ) {
            return;
        }

        if (enabled == mBackKeyTrapEnabled) {
            return;
        }

        if (backButtonCallback == null) {
            backButtonCallback = new OnBackInvokedCallback() {
                Handler mBackKeyHandler;

                @Override
                public void onBackInvoked() {
                    if (mBackKeyHandler == null) {
                        mBackKeyHandler = new Handler(Looper.getMainLooper());
                    }

                    onNativeKeyDown(KeyEvent.KEYCODE_BACK);
                    mBackKeyHandler.postDelayed(new Runnable() {
                        @Override
                        public void run() {
                            onNativeKeyUp(KeyEvent.KEYCODE_BACK);
                        }
                    }, 500);
                }
            };
        }

        if (enabled) {
            mSingleton.getOnBackInvokedDispatcher().registerOnBackInvokedCallback(OnBackInvokedDispatcher.PRIORITY_DEFAULT, backButtonCallback);
        }
        else {
            mSingleton.getOnBackInvokedDispatcher().unregisterOnBackInvokedCallback(backButtonCallback);
        }
        mBackKeyTrapEnabled = enabled;
    }

    // File dialog types
    private static final int SDL_FILEDIALOG_OPENFILE = 0;
    private static final int SDL_FILEDIALOG_SAVEFILE = 1;
    private static final int SDL_FILEDIALOG_OPENFOLDER = 2;

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);

        if (requestCode == 1001) {
            if (resultCode == Activity.RESULT_OK && data != null && data.getData() != null) {
                Uri uri = data.getData();
                SDLActivity.nativeFilePicked(uri.toString());
            } else {
                SDLActivity.nativeFilePickerCancelled();
            }
            return;
        }

        if (mFileDialogState != null && mFileDialogState.requestCode == requestCode) {
            String[] filelist = null;

            if (data != null && resultCode == Activity.RESULT_OK) {
                Uri singleFileUri = data.getData();

                if (singleFileUri == null) {
                    ClipData clipData = data.getClipData();
                    assert clipData != null;

                    filelist = new String[clipData.getItemCount()];

                    for (int i = 0; i < filelist.length; i++) {
                        String uri = clipData.getItemAt(i).getUri().toString();
                        filelist[i] = uri;
                    }
                } else {
                    if (mFileDialogState.type == SDL_FILEDIALOG_OPENFOLDER && mFileDialogState.persistable) {
                        mSingleton.getContentResolver().takePersistableUriPermission(singleFileUri,
                            Intent.FLAG_GRANT_READ_URI_PERMISSION |
                            Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
                    }
                    filelist = new String[]{singleFileUri.toString()};
                }
            } else {
                filelist = new String[0];
            }

            SDLActivity.onNativeFileDialog(requestCode, filelist, -1);
            mFileDialogState = null;
        }
    }

    public static void manualBackButton() {
        mSingleton.pressBackButton();
    }

    public void pressBackButton() {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                if (!SDLActivity.this.isFinishing()) {
                    SDLActivity.this.superOnBackPressed();
                }
            }
        });
    }

    public void superOnBackPressed() {
        super.onBackPressed();
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {

        if (SDLActivity.mBrokenLibraries) {
           return false;
        }

        int keyCode = event.getKeyCode();
        if (keyCode == KeyEvent.KEYCODE_VOLUME_DOWN ||
            keyCode == KeyEvent.KEYCODE_VOLUME_UP ||
            keyCode == KeyEvent.KEYCODE_CAMERA ||
            keyCode == KeyEvent.KEYCODE_ZOOM_IN ||
            keyCode == KeyEvent.KEYCODE_ZOOM_OUT
            ) {
            return false;
        }
        mDispatchingKeyEvent = true;
        boolean result = super.dispatchKeyEvent(event);
        mDispatchingKeyEvent = false;
        return result;
    }

    public static boolean dispatchingKeyEvent() {
        return mDispatchingKeyEvent;
    }

    public static void handleNativeState() {

        if (mNextNativeState == mCurrentNativeState) {
            return;
        }

        if (mNextNativeState == NativeState.INIT) {
            mCurrentNativeState = mNextNativeState;
            return;
        }

        if (mNextNativeState == NativeState.PAUSED) {
            if (mSDLThread != null) {
                nativePause();
            }
            if (mSurface != null) {
                mSurface.handlePause();
            }
            mCurrentNativeState = mNextNativeState;
            return;
        }

        if (mNextNativeState == NativeState.RESUMED) {
            boolean readyToRun = (mSurface == null) ? mIsResumedCalled
                    : (mSurface.mIsSurfaceReady && (mHasFocus || mHasMultiWindow) && mIsResumedCalled);
            if (readyToRun) {
                if (mSDLThread == null) {
                    mSDLThread = new Thread(new SDLMain(), "SDLThread");
                    if (mSurface != null) {
                        mSurface.enableSensor(Sensor.TYPE_ACCELEROMETER, true);
                    }
                    mSDLThread.start();
                } else {
                    nativeResume();
                }
                if (mSurface != null) {
                    mSurface.handleResume();
                }

                mCurrentNativeState = mNextNativeState;
            }
        }
    }

    // Messages from the SDLMain thread
    protected static final int COMMAND_CHANGE_TITLE = 1;
    protected static final int COMMAND_CHANGE_WINDOW_STYLE = 2;
    protected static final int COMMAND_TEXTEDIT_HIDE = 3;
    protected static final int COMMAND_SET_KEEP_SCREEN_ON = 5;
    protected static final int COMMAND_USER = 0x8000;

    protected static boolean mFullscreenModeActive;

    protected boolean onUnhandledMessage(int command, Object param) {
        return false;
    }

    protected static class SDLCommandHandler extends Handler {
        @Override
        public void handleMessage(Message msg) {
            Context context = getContext();
            if (context == null) {
                Log.e(TAG, "error handling message, getContext() returned null");
                return;
            }
            switch (msg.arg1) {
            case COMMAND_CHANGE_TITLE:
                if (context instanceof Activity) {
                    ((Activity) context).setTitle((String)msg.obj);
                } else {
                    Log.e(TAG, "error handling message, getContext() returned no Activity");
                }
                break;
            case COMMAND_CHANGE_WINDOW_STYLE:
                if (context instanceof Activity) {
                    Window window = ((Activity) context).getWindow();
                    if (window != null) {
                        if ((msg.obj instanceof Integer) && ((Integer) msg.obj != 0)) {
                            if (Build.VERSION.SDK_INT >= 30 /* Android 11 (R) */) {
                                final WindowInsetsController controller = window.getInsetsController();
                                if (controller != null) {
                                    controller.hide(WindowInsets.Type.systemBars());
                                    controller.setSystemBarsBehavior(
                                            WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                                }
                            } else {
                                int flags = View.SYSTEM_UI_FLAG_FULLSCREEN |
                                        View.SYSTEM_UI_FLAG_HIDE_NAVIGATION |
                                        View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY |
                                        View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN |
                                        View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION |
                                        View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.INVISIBLE;
                                window.getDecorView().setSystemUiVisibility(flags);
                                window.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
                                window.clearFlags(WindowManager.LayoutParams.FLAG_FORCE_NOT_FULLSCREEN);
                            }
                            SDLActivity.mFullscreenModeActive = true;
                        } else {
                            if (Build.VERSION.SDK_INT >= 30 /* Android 11 (R) */) {
                                final WindowInsetsController controller = window.getInsetsController();
                                if (controller != null) {
                                    controller.setSystemBarsBehavior(
                                            WindowInsetsController.BEHAVIOR_DEFAULT);
                                    controller.show(WindowInsets.Type.systemBars());
                                }
                            } else {
                                int flags = View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_VISIBLE;
                                window.getDecorView().setSystemUiVisibility(flags);
                                window.addFlags(WindowManager.LayoutParams.FLAG_FORCE_NOT_FULLSCREEN);
                                window.clearFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
                            }
                            SDLActivity.mFullscreenModeActive = false;
                        }
                        if (Build.VERSION.SDK_INT >= 30 /* Android 11 (R) */) {
                            window.getAttributes().layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS;
                        }
                    }
                } else {
                    Log.e(TAG, "error handling message, getContext() returned no Activity");
                }
                break;
            case COMMAND_TEXTEDIT_HIDE:
                if (mTextEdit != null) {
                    mTextEdit.setLayoutParams(new RelativeLayout.LayoutParams(0, 0));

                    InputMethodManager imm = (InputMethodManager) context.getSystemService(Context.INPUT_METHOD_SERVICE);
                    imm.hideSoftInputFromWindow(mTextEdit.getWindowToken(), 0);

                    onNativeScreenKeyboardHidden();

                    mSurface.requestFocus();
                }
                break;
            case COMMAND_SET_KEEP_SCREEN_ON:
            {
                if (context instanceof Activity) {
                    Window window = ((Activity) context).getWindow();
                    if (window != null) {
                        if ((msg.obj instanceof Integer) && ((Integer) msg.obj != 0)) {
                            window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
                        } else {
                            window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
                        }
                    }
                }
                break;
            }
            default:
                if ((context instanceof SDLActivity) && !((SDLActivity) context).onUnhandledMessage(msg.arg1, msg.obj)) {
                    Log.e(TAG, "error handling message, command is " + msg.arg1);
                }
            }
        }
    }

    Handler commandHandler = new SDLCommandHandler();

    protected boolean sendCommand(int command, Object data) {
        Message msg = commandHandler.obtainMessage();
        msg.arg1 = command;
        msg.obj = data;
        boolean result = commandHandler.sendMessage(msg);

        if (command == COMMAND_CHANGE_WINDOW_STYLE) {
            boolean bShouldWait = false;

            if (data instanceof Integer) {
                Display display = ((WindowManager) getSystemService(Context.WINDOW_SERVICE)).getDefaultDisplay();
                DisplayMetrics realMetrics = new DisplayMetrics();
                display.getRealMetrics(realMetrics);

                boolean bFullscreenLayout = (mSurface != null) &&
                        ((realMetrics.widthPixels == mSurface.getWidth()) &&
                        (realMetrics.heightPixels == mSurface.getHeight()));

                if ((Integer) data == 1) {
                    bShouldWait = !bFullscreenLayout;
                } else {
                    bShouldWait = bFullscreenLayout;
                }
            }

            if (bShouldWait && (getContext() != null)) {
                synchronized (getContext()) {
                    try {
                        getContext().wait(500);
                    } catch (InterruptedException ie) {
                        ie.printStackTrace();
                    }
                }
            }
        }

        return result;
    }

    // C functions we call
    public static native String nativeGetVersion();
    public static native void nativeSetupJNI();
    public static native int nativeGetCompiledSubsystems();
    public static native boolean nativeIsHIDAPIEnabled();
    public static native void nativeInitMainThread();
    public static native void nativeCleanupMainThread();
    public static native int nativeRunMain(String library, String function, Object arguments);
    public static native void nativeLowMemory();
    public static native void nativeSendQuit();
    public static native void nativeQuit();
    public static native void nativePause();
    public static native void nativeResume();
    public static native void nativeFocusChanged(boolean hasFocus);
    public static native void nativeFilePicked(String filepath);
    public static native void nativeFilePickerCancelled();
    public static native void onNativeDropFile(String filename);
    public static native void nativeSetScreenResolution(int surfaceWidth, int surfaceHeight, int deviceWidth, int deviceHeight, float density, float rate);
    public static native void onNativeResize();
    public static native void onNativeKeyDown(int keycode);
    public static native void onNativeKeyUp(int keycode);
    public static native boolean onNativeSoftReturnKey();
    public static native void onNativeKeyboardFocusLost();
    public static native void onNativeMouse(int button, int action, float x, float y, boolean relative);
    public static native void onNativeTouch(int touchDevId, int pointerFingerId,
                                            int action, float x,
                                            float y, float p);
    public static native void onNativePen(int penId, int device_type, int button, int action, float x, float y, float p);
    public static native void onNativeClipboardChanged();
    public static native void onNativeSurfaceCreated();
    public static native void onNativeSurfaceChanged();
    public static native void onNativeSurfaceDestroyed();
    public static native void onNativeScreenKeyboardShown();
    public static native void onNativeScreenKeyboardHidden();
    public static native String nativeGetHint(String name);
    public static native boolean nativeGetHintBoolean(String name, boolean default_value);
    public static native void nativeSetenv(String name, String value);
    public static native void nativeSetNaturalOrientation(int orientation);
    public static native void onNativeRotationChanged(int rotation);
    public static native void onNativeInsetsChanged(int left, int right, int top, int bottom);
    public static native void nativeAddTouch(int touchId, String name);
    public static native void nativePermissionResult(int requestCode, boolean result);
    public static native void onNativeLocaleChanged();
    public static native void onNativeDarkModeChanged(boolean enabled);
    public static native boolean nativeAllowRecreateActivity();
    public static native int nativeCheckSDLThreadCounter();
    public static native void onNativeFileDialog(int requestCode, String[] filelist, int filter);
    public static native void onNativePinchStart(float span_x, float span_y, float focus_x, float focus_y);
    public static native void onNativePinchUpdate(float scale, float span_x, float span_y, float focus_x, float focus_y);
    public static native void onNativePinchEnd();

    public static boolean setActivityTitle(String title) {
        return mSingleton.sendCommand(COMMAND_CHANGE_TITLE, title);
    }

    public static void setWindowStyle(boolean fullscreen) {
        mSingleton.sendCommand(COMMAND_CHANGE_WINDOW_STYLE, fullscreen ? 1 : 0);
    }

    public static void setOrientation(int w, int h, boolean resizable, String hint)
    {
        if (mSingleton != null) {
            mSingleton.setOrientationBis(w, h, resizable, hint);
        }
    }

    public void setOrientationBis(int w, int h, boolean resizable, String hint)
    {
        int orientation_landscape = -1;
        int orientation_portrait = -1;

        if (w <= 1 || h <= 1) {
            return;
        }

        if (hint.contains("LandscapeRight") && hint.contains("LandscapeLeft")) {
            orientation_landscape = ActivityInfo.SCREEN_ORIENTATION_USER_LANDSCAPE;
        } else if (hint.contains("LandscapeLeft")) {
            orientation_landscape = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE;
        } else if (hint.contains("LandscapeRight")) {
            orientation_landscape = ActivityInfo.SCREEN_ORIENTATION_REVERSE_LANDSCAPE;
        }

        boolean contains_Portrait = hint.contains("Portrait ") || hint.endsWith("Portrait");

        if (contains_Portrait && hint.contains("PortraitUpsideDown")) {
            orientation_portrait = ActivityInfo.SCREEN_ORIENTATION_USER_PORTRAIT;
        } else if (contains_Portrait) {
            orientation_portrait = ActivityInfo.SCREEN_ORIENTATION_PORTRAIT;
        } else if (hint.contains("PortraitUpsideDown")) {
            orientation_portrait = ActivityInfo.SCREEN_ORIENTATION_REVERSE_PORTRAIT;
        }

        boolean is_landscape_allowed = (orientation_landscape != -1);
        boolean is_portrait_allowed = (orientation_portrait != -1);
        int req;

        if (!is_portrait_allowed && !is_landscape_allowed) {
            if (resizable) {
                req = ActivityInfo.SCREEN_ORIENTATION_FULL_USER;
            } else {
                req = (w > h ? ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE : ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT);
            }
        } else {
            if (resizable) {
                if (is_portrait_allowed && is_landscape_allowed) {
                    req = ActivityInfo.SCREEN_ORIENTATION_FULL_USER;
                } else {
                    req = (is_landscape_allowed ? orientation_landscape : orientation_portrait);
                }
            } else {
                if (is_portrait_allowed && is_landscape_allowed) {
                    req = (w > h ? orientation_landscape : orientation_portrait);
                } else {
                    req = (is_landscape_allowed ? orientation_landscape : orientation_portrait);
                }
            }
        }

        Log.v(TAG, "setOrientation() requestedOrientation=" + req + " width=" + w +" height="+ h +" resizable=" + resizable + " hint=" + hint);
        mSingleton.setRequestedOrientation(req);
    }

    public static void minimizeWindow() {
        if (mSingleton == null) {
            return;
        }

        Intent startMain = new Intent(Intent.ACTION_MAIN);
        startMain.addCategory(Intent.CATEGORY_HOME);
        startMain.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        mSingleton.startActivity(startMain);
    }

    public static boolean shouldMinimizeOnFocusLoss() {
        return false;
    }

    public static boolean supportsRelativeMouse()
    {
        if (Build.VERSION.SDK_INT < 27 /* Android 8.1 (O_MR1) */ && isDeXMode()) {
            return false;
        }

        return SDLActivity.getMotionListener().supportsRelativeMouse();
    }

    public static boolean setRelativeMouseEnabled(boolean enabled)
    {
        if (enabled && !supportsRelativeMouse()) {
            return false;
        }

        return SDLActivity.getMotionListener().setRelativeMouseEnabled(enabled);
    }

    public static boolean sendMessage(int command, int param) {
        if (mSingleton == null) {
            return false;
        }
        return mSingleton.sendCommand(command, param);
    }

    public static Activity getContext() {
        return SDL.getContext();
    }

    public static boolean isAndroidTV() {
        UiModeManager uiModeManager = (UiModeManager) getContext().getSystemService(UI_MODE_SERVICE);
        if (uiModeManager.getCurrentModeType() == Configuration.UI_MODE_TYPE_TELEVISION) {
            return true;
        }
        if (Build.MANUFACTURER.equals("MINIX") && Build.MODEL.equals("NEO-U1")) {
            return true;
        }
        if (Build.MANUFACTURER.equals("Amlogic") &&
            (Build.MODEL.startsWith("TV") ||
             Build.MODEL.equals("X96-W") ||
             Build.MODEL.equals("A95X-R1"))) {
            return true;
        }
        return false;
    }

    public static boolean isVRHeadset() {
        if (Build.MANUFACTURER.equals("Oculus") && Build.MODEL.startsWith("Quest")) {
            return true;
        }
        if (Build.MANUFACTURER.equals("Pico")) {
            return true;
        }
        return false;
    }

    static String getDeviceFormFactor()
    {
        if (isAndroidTV()) {
            return "tv";
        } else if (isVRHeadset()) {
            return "headset";
        } else if (isTablet()) {
            return "tablet";
        } else {
            return "phone";
        }
    }

    public static double getDiagonal()
    {
        DisplayMetrics metrics = new DisplayMetrics();
        Activity activity = getContext();
        if (activity == null) {
            return 0.0;
        }
        activity.getWindowManager().getDefaultDisplay().getMetrics(metrics);

        double dWidthInches = metrics.widthPixels / (double)metrics.xdpi;
        double dHeightInches = metrics.heightPixels / (double)metrics.ydpi;

        return Math.sqrt((dWidthInches * dWidthInches) + (dHeightInches * dHeightInches));
    }

    public static boolean isTablet() {
        return (getDiagonal() >= 7.0);
    }

    public static boolean isChromebook() {
        if (getContext() != null) {
            if (getContext().getPackageManager().hasSystemFeature("org.chromium.arc")
                || getContext().getPackageManager().hasSystemFeature("org.chromium.arc.device_management")) {
                return true;
            }
        }

        boolean isChromebookEmulator = (Build.MODEL != null && Build.MODEL.startsWith("sdk_gpc_"));
        return isChromebookEmulator;
    }

    public static boolean isDeXMode() {
        if (Build.VERSION.SDK_INT < 24 /* Android 7.0 (N) */) {
            return false;
        }
        try {
            final Configuration config = getContext().getResources().getConfiguration();
            final Class<?> configClass = config.getClass();
            return configClass.getField("SEM_DESKTOP_MODE_ENABLED").getInt(configClass)
                    == configClass.getField("semDesktopModeEnabled").getInt(config);
        } catch(Exception ignored) {
            return false;
        }
    }

    public static boolean getManifestEnvironmentVariables() {
        try {
            if (getContext() == null) {
                return false;
            }

            ApplicationInfo applicationInfo = getContext().getPackageManager().getApplicationInfo(getContext().getPackageName(), PackageManager.GET_META_DATA);
            Bundle bundle = applicationInfo.metaData;
            if (bundle == null) {
                return false;
            }
            String prefix = "SDL_ENV.";
            final int trimLength = prefix.length();
            for (String key : bundle.keySet()) {
                if (key.startsWith(prefix)) {
                    String name = key.substring(trimLength);
                    String value = bundle.get(key).toString();
                    nativeSetenv(name, value);
                }
            }
            return true;
        } catch (Exception e) {
           Log.v(TAG, "exception " + e.toString());
        }
        return false;
    }

    public static View getContentView() {
        return mLayout;
    }

    static class ShowTextInputTask implements Runnable {
        static final int HEIGHT_PADDING = 15;

        public int input_type;
        public int x, y, w, h;

        public ShowTextInputTask(int input_type, int x, int y, int w, int h) {
            this.input_type = input_type;
            this.x = x;
            this.y = y;
            this.w = w;
            this.h = h;

            if (this.w <= 0) {
                this.w = 1;
            }
            if (this.h + HEIGHT_PADDING <= 0) {
                this.h = 1 - HEIGHT_PADDING;
            }
        }

        @Override
        public void run() {
            if (mLayout == null) {
                return;
            }

            RelativeLayout.LayoutParams params = new RelativeLayout.LayoutParams(w, h + HEIGHT_PADDING);
            params.leftMargin = x;
            params.topMargin = y;

            if (mTextEdit == null) {
                mTextEdit = new SDLDummyEdit(getContext());

                mLayout.addView(mTextEdit, params);
            } else {
                mTextEdit.setLayoutParams(params);
            }
            mTextEdit.setInputType(input_type);

            mTextEdit.setVisibility(View.VISIBLE);
            mTextEdit.requestFocus();

            InputMethodManager imm = (InputMethodManager) getContext().getSystemService(Context.INPUT_METHOD_SERVICE);
            imm.showSoftInput(mTextEdit, 0);

            if (imm.isAcceptingText()) {
                onNativeScreenKeyboardShown();
            }
        }
    }

    public static boolean showTextInput(int input_type, int x, int y, int w, int h) {
        if (mLayout == null) {
            return false;
        }

        return mSingleton.commandHandler.post(new ShowTextInputTask(input_type, x, y, w, h));
    }

    public static boolean isTextInputEvent(KeyEvent event) {
        if (event.isCtrlPressed()) {
            return false;
        }

        return event.isPrintingKey() || event.getKeyCode() == KeyEvent.KEYCODE_SPACE;
    }

    public static boolean handleKeyEvent(View v, int keyCode, KeyEvent event, InputConnection ic) {
        int deviceId = event.getDeviceId();
        int source = event.getSource();
        InputDevice device = InputDevice.getDevice(deviceId);

        if ((event.getFlags() & KeyEvent.FLAG_FALLBACK) != 0) {
            return true; 
        }

        if (source == InputDevice.SOURCE_UNKNOWN) {
            if (device != null) {
                source = device.getSources();
            }
        }

        if (SDL.isControllerManagerReady() && SDLControllerManager.isDeviceSDLJoystick(device)) {
            if (event.getAction() == KeyEvent.ACTION_DOWN) {
                if (SDLControllerManager.onNativePadDown(deviceId, keyCode, event.getScanCode())) {
                    return true;
                }
            } else if (event.getAction() == KeyEvent.ACTION_UP) {
                if (SDLControllerManager.onNativePadUp(deviceId, keyCode, event.getScanCode())) {
                    return true;
                }
            }
        }

        if ((source & InputDevice.SOURCE_MOUSE) == InputDevice.SOURCE_MOUSE) {
            if (SDLActivity.isVRHeadset()) {
                // Quest controller back button comes in as mouse
            } else {
                if ((keyCode == KeyEvent.KEYCODE_BACK) || (keyCode == KeyEvent.KEYCODE_FORWARD)) {
                    switch (event.getAction()) {
                    case KeyEvent.ACTION_DOWN:
                    case KeyEvent.ACTION_UP:
                        return true;
                    }
                }
            }
        }

        if (event.getAction() == KeyEvent.ACTION_DOWN) {
            onNativeKeyDown(keyCode);

            if (isTextInputEvent(event)) {
                if (ic != null) {
                    ic.commitText(String.valueOf((char) event.getUnicodeChar()), 1);
                } else {
                    SDLInputConnection.nativeCommitText(String.valueOf((char) event.getUnicodeChar()), 1);
                }
            }
            return true;
        } else if (event.getAction() == KeyEvent.ACTION_UP) {
            onNativeKeyUp(keyCode);
            return true;
        }

        return false;
    }

    public static Surface getNativeSurface() {
        if (SDLActivity.mSurface == null) {
            return null;
        }
        return SDLActivity.mSurface.getNativeSurface();
    }

    public static void initTouch() {
        int[] ids = InputDevice.getDeviceIds();

        for (int id : ids) {
            InputDevice device = InputDevice.getDevice(id);
            if (device != null && ((device.getSources() & InputDevice.SOURCE_TOUCHSCREEN) == InputDevice.SOURCE_TOUCHSCREEN
                    || device.isVirtual())) {

                nativeAddTouch(device.getId(), device.getName());
            }
        }
    }

    // Messagebox
    protected final int[] messageboxSelection = new int[1];

    public int messageboxShowMessageBox(
            final int flags,
            final String title,
            final String message,
            final int[] buttonFlags,
            final int[] buttonIds,
            final String[] buttonTexts,
            final int[] colors) {

        messageboxSelection[0] = -1;

        if ((buttonFlags.length != buttonIds.length) && (buttonIds.length != buttonTexts.length)) {
            return -1;
        }

        final Bundle args = new Bundle();
        args.putInt("flags", flags);
        args.putString("title", title);
        args.putString("message", message);
        args.putIntArray("buttonFlags", buttonFlags);
        args.putIntArray("buttonIds", buttonIds);
        args.putStringArray("buttonTexts", buttonTexts);
        args.putIntArray("colors", colors);

        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                messageboxCreateAndShow(args);
            }
        });

        synchronized (messageboxSelection) {
            try {
                messageboxSelection.wait();
            } catch (InterruptedException ex) {
                ex.printStackTrace();
                return -1;
            }
        }

        return messageboxSelection[0];
    }

    protected void messageboxCreateAndShow(Bundle args) {
        int[] colors = args.getIntArray("colors");
        int backgroundColor;
        int textColor;
        int buttonBorderColor;
        int buttonBackgroundColor;
        int buttonSelectedColor;
        if (colors != null) {
            int i = -1;
            backgroundColor = colors[++i];
            textColor = colors[++i];
            buttonBorderColor = colors[++i];
            buttonBackgroundColor = colors[++i];
            buttonSelectedColor = colors[++i];
        } else {
            backgroundColor = Color.TRANSPARENT;
            textColor = Color.TRANSPARENT;
            buttonBorderColor = Color.TRANSPARENT;
            buttonBackgroundColor = Color.TRANSPARENT;
            buttonSelectedColor = Color.TRANSPARENT;
        }

        final AlertDialog dialog = new AlertDialog.Builder(this).create();
        dialog.setTitle(args.getString("title"));
        dialog.setCancelable(false);
        dialog.setOnDismissListener(new DialogInterface.OnDismissListener() {
            @Override
            public void onDismiss(DialogInterface unused) {
                synchronized (messageboxSelection) {
                    messageboxSelection.notify();
                }
            }
        });

        TextView message = new TextView(this);
        message.setGravity(Gravity.CENTER);
        message.setText(args.getString("message"));
        if (textColor != Color.TRANSPARENT) {
            message.setTextColor(textColor);
        }

        int[] buttonFlags = args.getIntArray("buttonFlags");
        int[] buttonIds = args.getIntArray("buttonIds");
        String[] buttonTexts = args.getStringArray("buttonTexts");

        final SparseArray<Button> mapping = new SparseArray<Button>();

        LinearLayout buttons = new LinearLayout(this);
        buttons.setOrientation(LinearLayout.HORIZONTAL);
        buttons.setGravity(Gravity.CENTER);
        for (int i = 0; i < buttonTexts.length; ++i) {
            Button button = new Button(this);
            final int id = buttonIds[i];
            button.setOnClickListener(new View.OnClickListener() {
                @Override
                public void onClick(View v) {
                    messageboxSelection[0] = id;
                    dialog.dismiss();
                }
            });
            if (buttonFlags[i] != 0) {
                if ((buttonFlags[i] & 0x00000001) != 0) {
                    mapping.put(KeyEvent.KEYCODE_ENTER, button);
                }
                if ((buttonFlags[i] & 0x00000002) != 0) {
                    mapping.put(KeyEvent.KEYCODE_ESCAPE, button);
                }
            }
            button.setText(buttonTexts[i]);
            if (textColor != Color.TRANSPARENT) {
                button.setTextColor(textColor);
            }
            if (buttonBackgroundColor != Color.TRANSPARENT) {
                Drawable drawable = button.getBackground();
                if (drawable == null) {
                    button.setBackgroundColor(buttonBackgroundColor);
                } else {
                    drawable.setColorFilter(buttonBackgroundColor, PorterDuff.Mode.MULTIPLY);
                }
            }
            buttons.addView(button);
        }

        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.addView(message);
        content.addView(buttons);
        if (backgroundColor != Color.TRANSPARENT) {
            content.setBackgroundColor(backgroundColor);
        }

        dialog.setView(content);
        dialog.setOnKeyListener(new Dialog.OnKeyListener() {
            @Override
            public boolean onKey(DialogInterface d, int keyCode, KeyEvent event) {
                Button button = mapping.get(keyCode);
                if (button != null) {
                    if (event.getAction() == KeyEvent.ACTION_UP) {
                        button.performClick();
                    }
                    return true;
                }
                return false;
            }
        });

        dialog.show();
    }

    private final Runnable rehideSystemUi = new Runnable() {
        @Override
        public void run() {
            if (Build.VERSION.SDK_INT >= 30 /* Android 11 (R) */) {
                final WindowInsetsController controller =
                        SDLActivity.this.getWindow().getInsetsController();
                if (controller != null) {
                    controller.hide(WindowInsets.Type.systemBars());
                }
            } else {
                int flags = View.SYSTEM_UI_FLAG_FULLSCREEN |
                        View.SYSTEM_UI_FLAG_HIDE_NAVIGATION |
                        View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY |
                        View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN |
                        View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION |
                        View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.INVISIBLE;

                SDLActivity.this.getWindow().getDecorView().setSystemUiVisibility(flags);
            }
        }
    };

    public void onSystemUiVisibilityChange(int visibility) {
        if (SDLActivity.mFullscreenModeActive && ((visibility & View.SYSTEM_UI_FLAG_FULLSCREEN) == 0 || (visibility & View.SYSTEM_UI_FLAG_HIDE_NAVIGATION) == 0)) {
            Handler handler = getWindow().getDecorView().getHandler();
            if (handler != null) {
                handler.removeCallbacks(rehideSystemUi);
                handler.postDelayed(rehideSystemUi, 2000);
            }
        }
    }

    public static boolean clipboardHasText() {
        return mClipboardHandler.clipboardHasText();
    }

    public static String clipboardGetText() {
        return mClipboardHandler.clipboardGetText();
    }

    public static void clipboardSetText(String string) {
        mClipboardHandler.clipboardSetText(string);
    }

    public static int createCustomCursor(int[] colors, int width, int height, int hotSpotX, int hotSpotY) {
        Bitmap bitmap = Bitmap.createBitmap(colors, width, height, Bitmap.Config.ARGB_8888);
        ++mLastCursorID;

        if (Build.VERSION.SDK_INT >= 24 /* Android 7.0 (N) */) {
            try {
                mCursors.put(mLastCursorID, PointerIcon.create(bitmap, hotSpotX, hotSpotY));
            } catch (Exception e) {
                return 0;
            }
        } else {
            return 0;
        }
        return mLastCursorID;
    }

    public static void destroyCustomCursor(int cursorID) {
        if (Build.VERSION.SDK_INT >= 24 /* Android 7.0 (N) */) {
            try {
                mCursors.remove(cursorID);
            } catch (Exception e) {
            }
        }
    }

    public static boolean setCustomCursor(int cursorID) {
        if (Build.VERSION.SDK_INT >= 24 /* Android 7.0 (N) */) {
            try {
                mSurface.setPointerIcon(mCursors.get(cursorID));
            } catch (Exception e) {
                return false;
            }
        } else {
            return false;
        }
        return true;
    }

    public static boolean setSystemCursor(int cursorID) {
        int cursor_type = 0;
        switch (cursorID) {
        case SDL_SYSTEM_CURSOR_DEFAULT:
            cursor_type = 1000;
            break;
        case SDL_SYSTEM_CURSOR_TEXT:
            cursor_type = 1008;
            break;
        case SDL_SYSTEM_CURSOR_WAIT:
            cursor_type = 1004;
            break;
        case SDL_SYSTEM_CURSOR_CROSSHAIR:
            cursor_type = 1007;
            break;
        case SDL_SYSTEM_CURSOR_PROGRESS:
            cursor_type = 1004;
            break;
        case SDL_SYSTEM_CURSOR_NWSE_RESIZE:
            cursor_type = 1017;
            break;
        case SDL_SYSTEM_CURSOR_NESW_RESIZE:
            cursor_type = 1016;
            break;
        case SDL_SYSTEM_CURSOR_EW_RESIZE:
            cursor_type = 1014;
            break;
        case SDL_SYSTEM_CURSOR_NS_RESIZE:
            cursor_type = 1015;
            break;
        case SDL_SYSTEM_CURSOR_MOVE:
            cursor_type = 1020;
            break;
        case SDL_SYSTEM_CURSOR_NOT_ALLOWED:
            cursor_type = 1012;
            break;
        case SDL_SYSTEM_CURSOR_POINTER:
            cursor_type = 1002;
            break;
        case SDL_SYSTEM_CURSOR_NW_RESIZE:
            cursor_type = 1017;
            break;
        case SDL_SYSTEM_CURSOR_N_RESIZE:
            cursor_type = 1015;
            break;
        case SDL_SYSTEM_CURSOR_NE_RESIZE:
            cursor_type = 1016;
            break;
        case SDL_SYSTEM_CURSOR_E_RESIZE:
            cursor_type = 1014;
            break;
        case SDL_SYSTEM_CURSOR_SE_RESIZE:
            cursor_type = 1017;
            break;
        case SDL_SYSTEM_CURSOR_S_RESIZE:
            cursor_type = 1015;
            break;
        case SDL_SYSTEM_CURSOR_SW_RESIZE:
            cursor_type = 1016;
            break;
        case SDL_SYSTEM_CURSOR_W_RESIZE:
            cursor_type = 1014;
            break;
        case SDL_SYSTEM_CURSOR_CONTEXT_MENU:
            cursor_type = 1001;
            break;
        case SDL_SYSTEM_CURSOR_HELP:
            cursor_type = 1003;
            break;
        case SDL_SYSTEM_CURSOR_CELL:
            cursor_type = 1006;
            break;
        case SDL_SYSTEM_CURSOR_VERTICAL_TEXT:
            cursor_type = 1009;
            break;
        case SDL_SYSTEM_CURSOR_ALIAS:
            cursor_type = 1010;
            break;
        case SDL_SYSTEM_CURSOR_COPY:
            cursor_type = 1011;
            break;
        case SDL_SYSTEM_CURSOR_NO_DROP:
            cursor_type = 1012;
            break;
        case SDL_SYSTEM_CURSOR_GRAB:
            cursor_type = 1020;
            break;
        case SDL_SYSTEM_CURSOR_GRABBING:
            cursor_type = 1021;
            break;
        case SDL_SYSTEM_CURSOR_COL_RESIZE:
            cursor_type = 1014;
            break;
        case SDL_SYSTEM_CURSOR_ROW_RESIZE:
            cursor_type = 1015;
            break;
        case SDL_SYSTEM_CURSOR_ALL_SCROLL:
            cursor_type = 1013;
            break;
        case SDL_SYSTEM_CURSOR_ZOOM_IN:
            cursor_type = 1018;
            break;
        case SDL_SYSTEM_CURSOR_ZOOM_OUT:
            cursor_type = 1019;
            break;
        }
        if (Build.VERSION.SDK_INT >= 24 /* Android 7.0 (N) */) {
            try {
                mSurface.setPointerIcon(PointerIcon.getSystemIcon(getContext(), cursor_type));
            } catch (Exception e) {
                return false;
            }
        }
        return true;
    }

    public static void requestPermission(String permission, int requestCode) {
        if (Build.VERSION.SDK_INT < 23 /* Android 6.0 (M) */) {
            nativePermissionResult(requestCode, true);
            return;
        }

        Activity activity = getContext();
        if (activity.checkSelfPermission(permission) != PackageManager.PERMISSION_GRANTED) {
            activity.requestPermissions(new String[]{permission}, requestCode);
        } else {
            nativePermissionResult(requestCode, true);
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        boolean result = (grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED);
        nativePermissionResult(requestCode, result);
    }

     public static void copyAssetFilesToDir(String destpath) {
        destpath = destpath + "/";
        SDLActivity activity = (SDLActivity)getContext();
        activity.copyFileOrDir("", destpath); // copy all files in assets folder in my project
    }

    public void copyFileOrDir(String srcpath, String destpath) {
        AssetManager assetManager = this.getAssets();
        String assets[] = null;
        try {
            Log.i("tag", "copyFileOrDir() "+ srcpath);
            assets = assetManager.list(srcpath);
            if (assets.length == 0) {
                copyFile(srcpath, destpath);
            } else {
                String fullPath = destpath + srcpath;
                String[] copiedFolders = {"lang", "mods", "dynos", "palettes"};
                Log.i("tag", "path="+fullPath);
                File dir = new File(fullPath);
                if (!dir.exists())
                    if (!dir.mkdirs())
                        Log.i("tag", "could not create dir "+fullPath);
                for (int i = 0; i < assets.length; ++i) {
                    String p;
                    if (srcpath.equals(""))
                        p = "";
                    else
                        p = srcpath + "/";

                    String name = assets[i];

                    boolean shouldCopy = false;
                    for (String folder : copiedFolders) {
                        if (name.equals(folder) || srcpath.contains(folder)) {
                            shouldCopy = true;
                            break;
                        }
                    }

                    File destFolder = new File(destpath, name);
                    if (shouldCopy && !destFolder.exists()) {
                        copyFileOrDir(p + name, destpath);
                    }
                }
            }
        } catch (IOException ex) {
            Log.e("tag", "I/O Exception", ex);
        }
    }

    public void copyFile(String filename, String destpath) {
        AssetManager assetManager = this.getAssets();

        InputStream in = null;
        OutputStream out = null;
        String newFileName = null;
        try {
            Log.i("tag", "copyFile() "+filename);
            in = assetManager.open(filename);
            if (filename.endsWith(".jpg")) // extension was added to avoid compression on APK file
                newFileName = destpath + filename.substring(0, filename.length()-4);
            else
                newFileName = destpath + filename;
            out = new FileOutputStream(newFileName);

            byte[] buffer = new byte[1024];
            int read;
            while ((read = in.read(buffer)) != -1) {
                out.write(buffer, 0, read);
            }
            in.close();
            in = null;
            out.flush();
            out.close();
            out = null;
        } catch (Exception e) {
            Log.e("tag", "Exception in copyFile() of "+newFileName);
            Log.e("tag", "Exception in copyFile() "+e.toString());
        }
    }

    public static boolean openURL(String url)
    {
        try {
            Intent i = new Intent(Intent.ACTION_VIEW);
            i.setData(Uri.parse(url));

            int flags = Intent.FLAG_ACTIVITY_NO_HISTORY
                      | Intent.FLAG_ACTIVITY_MULTIPLE_TASK
                      | Intent.FLAG_ACTIVITY_NEW_DOCUMENT;
            i.addFlags(flags);

            mSingleton.startActivity(i);
        } catch (Exception ex) {
            return false;
        }
        return true;
    }

    public static boolean showToast(String message, int duration, int gravity, int xOffset, int yOffset)
    {
        if(null == mSingleton) {
            return false;
        }

        try
        {
            class OneShotTask implements Runnable {
                private final String mMessage;
                private final int mDuration;
                private final int mGravity;
                private final int mXOffset;
                private final int mYOffset;

                OneShotTask(String message, int duration, int gravity, int xOffset, int yOffset) {
                    mMessage  = message;
                    mDuration = duration;
                    mGravity  = gravity;
                    mXOffset  = xOffset;
                    mYOffset  = yOffset;
                }

                public void run() {
                    try
                    {
                        Toast toast = Toast.makeText(mSingleton, mMessage, mDuration);
                        if (mGravity >= 0) {
                            toast.setGravity(mGravity, mXOffset, mYOffset);
                        }
                        toast.show();
                    } catch(Exception ex) {
                        Log.e(TAG, ex.getMessage());
                    }
                }
            }
            mSingleton.runOnUiThread(new OneShotTask(message, duration, gravity, xOffset, yOffset));
        } catch(Exception ex) {
            return false;
        }
        return true;
    }

    public static int openFileDescriptor(String uri, String mode) throws Exception {
        if (mSingleton == null) {
            return -1;
        }

        try {
            ParcelFileDescriptor pfd = mSingleton.getContentResolver().openFileDescriptor(Uri.parse(uri), mode);
            return pfd != null ? pfd.detachFd() : -1;
        } catch (FileNotFoundException e) {
            e.printStackTrace();
            return -1;
        }
    }

    public static boolean showFileDialog(String[] filters, boolean allowMultiple,
        int type, String initialPath, int requestCode) {
        if (mSingleton == null) {
            return false;
        }

        ArrayList<String> mimes = new ArrayList<>();
        MimeTypeMap mimeTypeMap = MimeTypeMap.getSingleton();
        if (filters != null && type != SDL_FILEDIALOG_OPENFOLDER) {
            for (String pattern : filters) {
                String[] extensions = pattern.split(";");

                if (extensions.length == 1 && extensions[0].equals("*")) {
                    mimes.add("*/*");
                } else {
                    for (String ext : extensions) {
                        String mime = mimeTypeMap.getMimeTypeFromExtension(ext);
                        if (mime != null) {
                            mimes.add(mime);
                        }
                    }
                }
            }
        }

        Uri initialPathUri = null;

        if (initialPath != null && !initialPath.isEmpty()) {
            try {
                initialPathUri = Uri.parse(initialPath);
            } catch (Exception e) {
                Log.e(TAG, "Failed to parse initial path URI, ignoring initial path", e);
            }
        }

        boolean persistable = SDLActivity.nativeGetHintBoolean("SDL_ANDROID_ALLOW_PERSISTENT_FOLDER_ACCESS", false);

        String action;
        switch (type) {
            case SDL_FILEDIALOG_OPENFILE:
                action = Intent.ACTION_OPEN_DOCUMENT;
                break;
            case SDL_FILEDIALOG_SAVEFILE:
                action = Intent.ACTION_CREATE_DOCUMENT;
                allowMultiple = false;
                break;
            case SDL_FILEDIALOG_OPENFOLDER:
                action = Intent.ACTION_OPEN_DOCUMENT_TREE;
                break;
            default:
                Log.e(TAG, "Unsupported file dialog type: " + type);
                return false;
        }

        Intent intent = new Intent(action);
        if (type != SDL_FILEDIALOG_OPENFOLDER) {
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, allowMultiple);
            switch (mimes.size()) {
                case 0:
                    intent.setType("*/*");
                    break;
                case 1:
                    intent.setType(mimes.get(0));
                    break;
                default:
                    intent.setType("*/*");
                    intent.putExtra(Intent.EXTRA_MIME_TYPES, mimes.toArray(new String[]{}));
            }
        } else {
            int intent_flags = Intent.FLAG_GRANT_READ_URI_PERMISSION |
                Intent.FLAG_GRANT_WRITE_URI_PERMISSION;
            if (persistable) {
                intent_flags |= Intent.FLAG_GRANT_PREFIX_URI_PERMISSION |
                    Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION;
            }
            intent.addFlags(intent_flags);
        }

        if (initialPathUri != null) {
            intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI, initialPathUri);
        }

        if (type == SDL_FILEDIALOG_SAVEFILE && initialPath != null && !initialPath.isEmpty() &&
            !initialPath.endsWith("/") && !initialPath.endsWith("\\")) {
            String title = initialPath;
            int lastSeparator = Math.max(title.lastIndexOf('/'), title.lastIndexOf('\\'));
            if (lastSeparator >= 0) {
                title = title.substring(lastSeparator + 1);
            }
            if (!title.isEmpty()) {
                intent.putExtra(Intent.EXTRA_TITLE, title);
            }
        }

        try {
            mSingleton.startActivityForResult(intent, requestCode);
        } catch (ActivityNotFoundException e) {
            Log.e(TAG, "Unable to open dialog.", e);
            return false;
        }

        mFileDialogState = new SDLFileDialogState();
        mFileDialogState.requestCode = requestCode;
        mFileDialogState.type = type;
        mFileDialogState.persistable = persistable;

        return true;
    }

    static class SDLFileDialogState {
        int requestCode;
        int type;
        boolean persistable;
    }

    public static String getPreferredLocales() {
        String result = "";
        if (Build.VERSION.SDK_INT >= 24 /* Android 7 (N) */) {
            LocaleList locales = LocaleList.getAdjustedDefault();
            for (int i = 0; i < locales.size(); i++) {
                if (i != 0) result += ",";
                result += formatLocale(locales.get(i));
            }
        } else if (mCurrentLocale != null) {
            result = formatLocale(mCurrentLocale);
        }
        return result;
    }

    public static String formatLocale(Locale locale) {
        String result = "";
        String lang = "";
        if (locale.getLanguage() == "in") {
            lang = "id";
        } else if (locale.getLanguage() == "") {
            lang = "und";
        } else {
            lang = locale.getLanguage();
        }

        if (locale.getCountry() == "") {
            result = lang;
        } else {
            result = lang + "_" + locale.getCountry();
        }
        return result;
    }
}

class SDLMain implements Runnable {
    @Override
    public void run() {
        try {
            android.os.Process.setThreadPriority(android.os.Process.THREAD_PRIORITY_DISPLAY);
        } catch (Exception e) {
            Log.v("SDL", "modify thread properties failed " + e.toString());
        }

        SDLActivity.nativeInitMainThread();
        SDLActivity.mSingleton.main();
        SDLActivity.nativeCleanupMainThread();

        if (SDLActivity.mSingleton != null && !SDLActivity.mSingleton.isFinishing()) {
            SDLActivity.mSDLThread = null;
            SDLActivity.mSDLMainFinished = true;
            SDLActivity.mSingleton.finish();
        }
    }
}

class SDLClipboardHandler implements ClipboardManager.OnPrimaryClipChangedListener {

    protected ClipboardManager mClipMgr;

    SDLClipboardHandler() {
       mClipMgr = (ClipboardManager) SDL.getContext().getSystemService(Context.CLIPBOARD_SERVICE);
       mClipMgr.addPrimaryClipChangedListener(this);
    }

    public boolean clipboardHasText() {
        if (Build.VERSION.SDK_INT >= 28 /* Android 9 (P) */) {
            return mClipMgr.hasPrimaryClip();
        } else {
            return mClipMgr.hasText();
        }
    }

    public String clipboardGetText() {
        ClipData clip = mClipMgr.getPrimaryClip();
        if (clip != null) {
            ClipData.Item item = clip.getItemAt(0);
            if (item != null) {
                CharSequence text = item.getText();
                if (text != null) {
                    return text.toString();
                }
            }
        }
        return null;
    }

    public void clipboardSetText(String string) {
        mClipMgr.removePrimaryClipChangedListener(this);
        if (string.isEmpty()) {
            if (Build.VERSION.SDK_INT >= 28 /* Android 9 (P) */) {
                mClipMgr.clearPrimaryClip();
            } else {
                ClipData clip = ClipData.newPlainText(null, "");
                mClipMgr.setPrimaryClip(clip);
            }
        } else {
            ClipData clip = ClipData.newPlainText(null, string);
            mClipMgr.setPrimaryClip(clip);
        }
        mClipMgr.addPrimaryClipChangedListener(this);
    }

    @Override
    public void onPrimaryClipChanged() {
        SDLActivity.onNativeClipboardChanged();
    }
}