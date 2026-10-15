import java.util.Properties

plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.compose)
    alias(libs.plugins.kotlin.serialization)
}

// Release signing from keystore.properties (not in git) or from environment variables (CI):
// storeFile, storePassword, keyAlias, keyPassword.
val signing = Properties().apply {
    val file = rootProject.file("keystore.properties")
    if (file.exists()) file.inputStream().use { load(it) }
    System.getenv("IMAGO_KEYSTORE_FILE")?.let { setProperty("storeFile", it) }
    System.getenv("IMAGO_KEYSTORE_PASSWORD")?.let { setProperty("storePassword", it) }
    System.getenv("IMAGO_KEY_ALIAS")?.let { setProperty("keyAlias", it) }
    System.getenv("IMAGO_KEY_PASSWORD")?.let { setProperty("keyPassword", it) }
}
val hasReleaseSigning = signing.getProperty("storeFile")?.let { rootProject.file(it).exists() } == true

android {
    namespace = "aero.imago.payloadcontrol"
    compileSdk = 37
    ndkVersion = "29.0.14206865"

    defaultConfig {
        applicationId = "aero.imago.payloadcontrol"
        // Android 7.0: phones and tablets from about 2016 on.
        minSdk = 24
        targetSdk = 36
        versionCode = 1
        versionName = "0.1.0"

        ndk {
            // 32-bit ARM for older devices, x86_64 for the emulator.
            abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64")
        }
        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_STL=c++_static")
            }
        }
    }

    signingConfigs {
        if (hasReleaseSigning) {
            create("release") {
                storeFile = rootProject.file(signing.getProperty("storeFile"))
                storePassword = signing.getProperty("storePassword")
                keyAlias = signing.getProperty("keyAlias")
                keyPassword = signing.getProperty("keyPassword")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            if (hasReleaseSigning) signingConfig = signingConfigs.getByName("release")
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.31.6"
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    buildFeatures {
        compose = true
    }

    testOptions {
        // Android framework calls in plain JVM tests return defaults instead of throwing.
        unitTests.isReturnDefaultValues = true
    }

    lint {
        // Target 36, not 37: Android 17 asks apps targeting it for a local network permission,
        // to be added once it can be tested on a device.
        disable += "OldTargetApi"
    }

    packaging {
        jniLibs.useLegacyPackaging = false
    }
}

dependencies {
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.activity.compose)
    implementation(libs.androidx.lifecycle.runtime.compose)
    implementation(platform(libs.compose.bom))
    implementation(libs.compose.ui)
    implementation(libs.compose.foundation)
    implementation(libs.compose.material3)
    implementation(libs.compose.icons.extended)
    implementation(libs.compose.ui.tooling.preview)
    debugImplementation(libs.compose.ui.tooling)
    implementation(libs.kotlinx.serialization.json)
    testImplementation(libs.junit)
}
