plugins {
    `kotlin-dsl`
}

repositories {
    System.getenv("HOP_MAVEN_CENTRAL_MIRROR")?.let { maven { url = uri(it) } }
    gradlePluginPortal()
}

kotlin {
    jvmToolchain(17)
}

gradlePlugin {
    plugins {
        register("settingsGradlePropertyDelegate") {
            id = "settings-property-delegate"
            implementationClass = "SettingsPropertyDelegate"
        }

        register("projectGradlePropertyDelegate") {
            id = "property-delegate"
            implementationClass = "ProjectPropertyDelegate"
        }
    }
}
