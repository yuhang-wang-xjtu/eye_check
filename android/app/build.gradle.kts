plugins { id("com.android.application"); id("org.jetbrains.kotlin.android") }
android {
    namespace = "cn.eyebreak.app"
    compileSdk = 35
    buildToolsVersion = "35.0.0"
    defaultConfig { applicationId = "cn.eyebreak.app"; minSdk = 26; targetSdk = 35; versionCode = 5; versionName = "0.2.2" }
    compileOptions { sourceCompatibility = JavaVersion.VERSION_17; targetCompatibility = JavaVersion.VERSION_17 }
    kotlinOptions { jvmTarget = "17" }
    buildFeatures { buildConfig = true }
    testOptions { unitTests.isReturnDefaultValues = true }
}
dependencies { testImplementation("junit:junit:4.13.2") }
