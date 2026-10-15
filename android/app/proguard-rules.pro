# Called from native code (src/main/cpp): keep the classes and methods JNI looks up by name.
-keep class aero.imago.payloadcontrol.core.NativeCore { *; }
-keep class aero.imago.payloadcontrol.core.VideoRenderer { void onVideoSize(int, int); }
-keep class aero.imago.payloadcontrol.core.PayloadSession$Listener { void onEvent(int, int, int, int, byte[]); }
