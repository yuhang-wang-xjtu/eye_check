package cn.eyebreak.app

import org.junit.Assert.*
import org.junit.Test

class ReleaseVersionTest {
    @Test fun compareNumericallyAndRejectDowngrades() {
        assertTrue(ReleaseVersion.newer("v0.10.0", "0.2.0"))
        assertTrue(ReleaseVersion.newer("v1.0.0", "0.99.99"))
        assertTrue(ReleaseVersion.newer("v0.2.0", "0.1.1-prototype"))
        assertFalse(ReleaseVersion.newer("v0.2.0", "0.2.0"))
        assertFalse(ReleaseVersion.newer("v0.1.0", "0.2.0"))
    }
    @Test fun rejectUnrecognizedAndPrereleaseVersions() {
        listOf("v1.2", "1.2.3.4", "v1.2.3-beta", "v9999999999.0.0", "https://host/1.2.3", "v-1.0.0").forEach {
            assertNull(ReleaseVersion.parts(it)); assertFalse(ReleaseVersion.newer(it, "0.2.0"))
        }
    }
    @Test fun restrictDownloadToExactRepositoryTagAndApk() {
        val valid = "${ReleaseVersion.REPOSITORY}/releases/download/v0.3.0/EyeBreak-Android-v0.3.0.apk"
        assertTrue(ReleaseVersion.validAsset(valid, "v0.3.0"))
        listOf(valid.replace("https:", "http:"), valid.replace("github.com", "github.com.evil.test"),
            valid.replace("eye_check/", "another/"), "$valid?x=1", "$valid#x", valid.replace("Android", "Windows"),
            valid.replace("github.com/", "github.com:443/"), valid.replace("github.com/", "user@github.com/")).forEach {
            assertFalse(ReleaseVersion.validAsset(it, "v0.3.0"))
        }
        assertFalse(ReleaseVersion.validAsset(valid, "v0.4.0"))
    }
    @Test fun requireFullSha256Digest() {
        assertEquals("a".repeat(64), ReleaseVersion.digest("sha256:" + "A".repeat(64)))
        listOf("", "sha256:abc", "md5:" + "a".repeat(64), "sha256:" + "g".repeat(64)).forEach { assertNull(ReleaseVersion.digest(it)) }
    }
}
