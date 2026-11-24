pluginManagement {
    repositories {
        System.getenv("HOP_MAVEN_CENTRAL_MIRROR")?.let { maven { url = uri(it) } }
        gradlePluginPortal()
        mavenCentral()
    }
}
