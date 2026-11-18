import java.util.Properties

plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.compose)
    alias(libs.plugins.kotlin.serialization)
}

// One fixed key for every APK, so a new build installs over the last one and keeps what the app
// stored on the headset (map tiles, profiles, logs). With the CI runner's throwaway debug key
// each build had a new signature, the headset refused it as an update, and uninstalling first
// deleted the downloaded map (2026-11-18). From keystore.properties (not in git) or from
// environment variables (CI, from the repository's secrets): storeFile, storePassword,
// keyAlias, keyPassword. Without them the usual debug key signs, as before.
val signing = Properties().apply {
    val file = rootProject.file("keystore.properties")
    if (file.exists()) file.inputStream().use { load(it) }
    System.getenv("IMAGO_QUEST_KEYSTORE_FILE")?.let { setProperty("storeFile", it) }
    System.getenv("IMAGO_QUEST_KEYSTORE_PASSWORD")?.let { setProperty("storePassword", it) }
    System.getenv("IMAGO_QUEST_KEY_ALIAS")?.let { setProperty("keyAlias", it) }
    System.getenv("IMAGO_QUEST_KEY_PASSWORD")?.let { setProperty("keyPassword", it) }
}
val hasFixedSigning = signing.getProperty("storeFile")?.let { rootProject.file(it).exists() } == true

android {
    namespace = "aero.imago.payloadcontrol.quest"
    compileSdk = 37
    ndkVersion = "29.0.14206865"

    defaultConfig {
        applicationId = "aero.imago.payloadcontrol.quest"
        // Meta Quest 2/3/Pro run Android 12L (API 32); Meta asks apps to target 32.
        minSdk = 32
        targetSdk = 32
        versionCode = 1
        versionName = "0.1.0"

        ndk {
            abiFilters += listOf("arm64-v8a")
        }
        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_STL=c++_static")
            }
        }
    }

    signingConfigs {
        if (hasFixedSigning) {
            create("fixed") {
                storeFile = rootProject.file(signing.getProperty("storeFile"))
                storePassword = signing.getProperty("storePassword")
                keyAlias = signing.getProperty("keyAlias")
                keyPassword = signing.getProperty("keyPassword")
            }
        }
    }

    buildTypes {
        debug {
            if (hasFixedSigning) signingConfig = signingConfigs.getByName("fixed")
        }
        release {
            isMinifyEnabled = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            if (hasFixedSigning) signingConfig = signingConfigs.getByName("fixed")
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.31.6"
        }
    }

    buildFeatures {
        // The OpenXR loader AAR ships its headers and library as a prefab module.
        prefab = true
        // The 2D panel.
        compose = true
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    testOptions {
        unitTests.isReturnDefaultValues = true
        // Robolectric reads the fonts of the interface from the merged resources.
        unitTests.isIncludeAndroidResources = true
    }

    lint {
        // Target 32 on purpose: what Meta's store asks for on the Quest.
        disable += listOf("OldTargetApi", "ExpiredTargetSdkVersion")
    }

    packaging {
        jniLibs.useLegacyPackaging = false
    }
}

dependencies {
    implementation(libs.openxr.loader)
    implementation(libs.kotlinx.serialization.json)
    implementation(libs.androidx.activity.compose)
    implementation(platform(libs.compose.bom))
    implementation(libs.compose.ui)
    implementation(libs.compose.foundation)
    implementation(libs.compose.material3)
    testImplementation(libs.junit)
    testImplementation(libs.robolectric)
}
