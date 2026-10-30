package org.crazyromteam.maetplayer

import android.view.Surface

class MaetDecoder {
    external fun decodeSpi(spiPath: String, outPath: String): String
    external fun previewSpi(spiPath: String, surface: Surface): Boolean
    external fun getSpiDimensions(spiPath: String): IntArray? // Returns [width, height]

    companion object {
        init {
            System.loadLibrary("maet_jni")
        }
    }
}
