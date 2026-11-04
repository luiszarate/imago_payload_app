# Called from native code (src/main/cpp): keep the classes and methods JNI looks up by name.
-keep class aero.imago.payloadcontrol.quest.NativeBridge { *; }
-keep class aero.imago.payloadcontrol.quest.QuestActivity { void onVideoSize(int, int); void onNotice(byte[]); }
