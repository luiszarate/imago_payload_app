plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.serialization)
}

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

    buildTypes {
        release {
            isMinifyEnabled = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
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
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    testOptions {
        unitTests.isReturnDefaultValues = true
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
    testImplementation(libs.junit)
}
