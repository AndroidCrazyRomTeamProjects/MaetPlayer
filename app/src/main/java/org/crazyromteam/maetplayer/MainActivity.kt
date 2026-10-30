package org.crazyromteam.maetplayer

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Color
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.provider.DocumentsContract
import android.provider.Settings
import android.view.Gravity
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.constraintlayout.widget.ConstraintLayout
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import com.google.android.material.button.MaterialButton
import com.google.android.material.card.MaterialCardView
import java.io.File

class MainActivity : AppCompatActivity() {

    private var selectedFile: File? = null
    private var isSurfaceReady = false
    private var selectedButtonView: View? = null
    private var customFolder: File? = null

    private lateinit var surfaceView: SurfaceView
    private lateinit var previewPlaceholder: View
    private lateinit var selectedFileTitle: TextView
    private lateinit var statusText: TextView
    private lateinit var fileCountText: TextView
    private lateinit var btnBatchConvert: MaterialButton
    private lateinit var btnConvert: MaterialButton
    private lateinit var btnRefresh: MaterialButton

    private lateinit var btnCustomSpiPath: MaterialButton
    private lateinit var fileListContainer: LinearLayout

    private val maetDecoder = MaetDecoder()

    private val openDirectoryLauncher = registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        uri?.let {
            // Persist access permissions
            contentResolver.takePersistableUriPermission(it, Intent.FLAG_GRANT_READ_URI_PERMISSION)
            
            val path = getPathFromUri(it)
            if (path != null) {
                val dir = File(path)
                if (dir.exists() && dir.isDirectory) {
                    customFolder = dir
                    populateFiles()
                    Toast.makeText(this, "Folder changed to: ${dir.absolutePath}", Toast.LENGTH_SHORT).show()
                } else {
                    Toast.makeText(this, "Selected path is not a valid directory", Toast.LENGTH_SHORT).show()
                }
            } else {
                Toast.makeText(this, "Could not resolve physical path for selection", Toast.LENGTH_SHORT).show()
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        surfaceView = findViewById(R.id.surfaceView)
        previewPlaceholder = findViewById(R.id.previewPlaceholder)
        selectedFileTitle = findViewById(R.id.selectedFileTitle)
        statusText = findViewById(R.id.statusText)
        fileCountText = findViewById(R.id.fileCountText)
        btnBatchConvert = findViewById(R.id.btnBatchConvert)
        btnConvert = findViewById(R.id.btnConvert)
        btnRefresh = findViewById(R.id.btnRefresh)
        btnCustomSpiPath = findViewById(R.id.customSpiPath)

        fileListContainer = findViewById(R.id.fileListContainer)

        surfaceView.holder.addCallback(object : SurfaceHolder.Callback {
            override fun surfaceCreated(holder: SurfaceHolder) {
                isSurfaceReady = true
                // If a file was already selected, render it now
                selectedFile?.let { renderPreview(it) }
            }

            override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {}

            override fun surfaceDestroyed(holder: SurfaceHolder) {
                isSurfaceReady = false
            }
        })

        btnBatchConvert.setOnClickListener {
            if (checkStoragePermissions()) {
                startBatchConversion()
            } else {
                requestStoragePermissions()
            }
        }

        btnConvert.setOnClickListener {
            if (checkStoragePermissions()) {
                startConversion()
            } else {
                requestStoragePermissions()
            }
        }

        btnRefresh.setOnClickListener {
            populateFiles()
        }

        btnCustomSpiPath.setOnClickListener {
            openDirectoryLauncher.launch(null)
        }

        populateFiles()
    }

    private fun renderPreview(file: File) {
        statusText.text = "Rendering preview..."
        previewPlaceholder.visibility = View.GONE

        Thread {
            try {
                // First, get dimensions to update UI aspect ratio
                val dims = maetDecoder.getSpiDimensions(file.absolutePath)
                if (dims != null && dims.size == 2) {
                    val w = dims[0]
                    val h = dims[1]
                    runOnUiThread {
                        val params = surfaceView.layoutParams as ConstraintLayout.LayoutParams
                        params.dimensionRatio = "$w:$h"
                        surfaceView.layoutParams = params
                    }
                }

                // Wait slightly for surface to adjust if necessary (or just proceed)
                if (!isSurfaceReady) {
                    runOnUiThread { statusText.text = "Waiting for surface..." }
                    var timeout = 0
                    while (!isSurfaceReady && timeout < 50) {
                        Thread.sleep(20)
                        timeout++
                    }
                }

                if (isSurfaceReady) {
                    val ok = maetDecoder.previewSpi(file.absolutePath, surfaceView.holder.surface)
                    runOnUiThread {
                        if (ok) {
                            statusText.text = "Preview: ${dims?.get(0)}x${dims?.get(1)}"
                        } else {
                            previewPlaceholder.visibility = View.VISIBLE
                            statusText.text = "Preview failed: decoder error."
                        }
                    }
                } else {
                    runOnUiThread {
                        previewPlaceholder.visibility = View.VISIBLE
                        statusText.text = "Surface not ready."
                    }
                }
            } catch (e: Exception) {
                runOnUiThread {
                    previewPlaceholder.visibility = View.VISIBLE
                    statusText.text = "Preview exception: ${e.message}"
                }
            }
        }.start()
    }

    private fun populateFiles() {
        fileListContainer.removeAllViews()
        selectedButtonView = null
        selectedFile = null
        btnConvert.isEnabled = false
        selectedFileTitle.text = "No file selected"
        statusText.text = "Tap a file below to select"

        val files = loadFiles()
        fileCountText.text = "${files.size} files"

        if (files.isEmpty()) {
            val emptyCard = MaterialCardView(this).apply {
                radius = 24f
                setCardBackgroundColor(0xFF1E1E1E.toInt())
                strokeColor = 0xFF2E2E2E.toInt()
                strokeWidth = 2
            }
            val emptyText = TextView(this).apply {
                text = if (customFolder != null) "No .spi files found in ${customFolder?.name}" else "No .spi files found in system paths"
                setTextColor(0xFFEF4444.toInt())
                gravity = Gravity.CENTER
                setPadding(24, 32, 24, 32)
                textSize = 14f
            }
            emptyCard.addView(emptyText)
            fileListContainer.addView(emptyCard)
            return
        }

        for (file in files) {
            val card = MaterialCardView(this).apply {
                radius = 24f
                cardElevation = 2f
                setCardBackgroundColor(0xFF1C1C1E.toInt())
                strokeColor = 0xFF2C2C2E.toInt()
                strokeWidth = 2
                isClickable = true
                isFocusable = true
                layoutParams = LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT
                ).apply {
                    bottomMargin = 16
                }
            }

            val itemLayout = LinearLayout(this).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.CENTER_VERTICAL
                setPadding(24, 20, 24, 20)
            }

            val iconText = TextView(this).apply {
                text = "🖼️"
                textSize = 20f
                setPadding(0, 0, 24, 0)
            }

            val textLayout = LinearLayout(this).apply {
                orientation = LinearLayout.VERTICAL
                layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
            }

            val nameView = TextView(this).apply {
                text = file.name
                setTextColor(0xFFFFFFFF.toInt())
                textSize = 14f
                typeface = android.graphics.Typeface.DEFAULT_BOLD
            }

            val sizeKb = file.length() / 1024
            val subView = TextView(this).apply {
                text = "${sizeKb} KB • ${file.parent}"
                setTextColor(0xFF8E8E93.toInt())
                textSize = 11f
            }

            textLayout.addView(nameView)
            textLayout.addView(subView)

            itemLayout.addView(iconText)
            itemLayout.addView(textLayout)
            card.addView(itemLayout)

            card.setOnClickListener {
                // Highlight selected item
                (selectedButtonView as? MaterialCardView)?.strokeColor = 0xFF2C2C2E.toInt()
                (selectedButtonView as? MaterialCardView)?.setCardBackgroundColor(0xFF1C1C1E.toInt())

                card.strokeColor = 0xFF38BDF8.toInt()
                card.setCardBackgroundColor(0xFF16293A.toInt())
                selectedButtonView = card

                selectedFile = file
                selectedFileTitle.text = file.name
                statusText.text = "Selected (${file.length() / 1024} KB). Tap Preview or Convert."
                btnConvert.isEnabled = true

                // Auto render preview when tapped
                renderPreview(file)
            }

            fileListContainer.addView(card)
        }
    }

    private fun startConversion(fileToConvert: File? = null) {
        val file = fileToConvert ?: selectedFile ?: return

        if (fileToConvert == null) {
            statusText.text = "Converting ${file.name} to PNG..."
        }

        btnConvert.isEnabled = false
        btnBatchConvert.isEnabled = false

        val task = Runnable {
            try {
                val root = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOCUMENTS)
                val dir = File(root, "MaetPlayer")
                if (!dir.exists() && !dir.mkdirs()) {
                    runOnUiThread {
                        statusText.text = "Error: Could not create output directory\n${dir.absolutePath}"
                        btnConvert.isEnabled = true
                        btnBatchConvert.isEnabled = true
                    }
                    return@Runnable
                }

                val outFile = File(dir, "${file.nameWithoutExtension}.png")
                val result = maetDecoder.decodeSpi(file.absolutePath, outFile.absolutePath)

                runOnUiThread {
                    if (fileToConvert == null) {
                        statusText.text = "$result\nSaved: ${outFile.absolutePath}"
                        btnConvert.isEnabled = selectedFile != null
                        btnBatchConvert.isEnabled = true
                    }
                }
            } catch (e: Exception) {
                runOnUiThread {
                    if (fileToConvert == null) {
                        statusText.text = "Exception: ${e.message}"
                        btnConvert.isEnabled = selectedFile != null
                        btnBatchConvert.isEnabled = true
                    }
                }
            }
        }

        if (fileToConvert == null) {
            Thread(task).start()
        } else {
            task.run()
        }
    }

    private fun startBatchConversion() {
        val files = loadFiles()
        if (files.isEmpty()) return

        btnConvert.isEnabled = false
        btnBatchConvert.isEnabled = false
        statusText.text = "Starting batch conversion of ${files.size} files..."

        Thread {
            var count = 0
            val startTime = System.currentTimeMillis()

            for (file in files) {
                count++
                val currentCount = count
                runOnUiThread {
                    statusText.text = "Batch: Converting $currentCount/${files.size}\n${file.name}"
                }
                startConversion(file)
            }

            val duration = (System.currentTimeMillis() - startTime) / 1000
            runOnUiThread {
                statusText.text = "Batch complete!\nProcessed $count files in ${duration}s.\nCheck Documents/MaetPlayer"
                btnConvert.isEnabled = selectedFile != null
                btnBatchConvert.isEnabled = true
                Toast.makeText(this, "Batch conversion finished", Toast.LENGTH_SHORT).show()
            }
        }.start()
    }

    private fun checkStoragePermissions(): Boolean {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            Environment.isExternalStorageManager()
        } else {
            val write = ContextCompat.checkSelfPermission(this, Manifest.permission.WRITE_EXTERNAL_STORAGE)
            val read = ContextCompat.checkSelfPermission(this, Manifest.permission.READ_EXTERNAL_STORAGE)
            write == PackageManager.PERMISSION_GRANTED && read == PackageManager.PERMISSION_GRANTED
        }
    }

    private fun requestStoragePermissions() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            try {
                val intent = Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION)
                intent.addCategory("android.intent.category.DEFAULT")
                intent.data = Uri.parse("package:$packageName")
                startActivity(intent)
                Toast.makeText(this, "Please allow 'All files access' for this app", Toast.LENGTH_LONG).show()
            } catch (e: Exception) {
                val intent = Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION)
                startActivity(intent)
            }
        } else {
            ActivityCompat.requestPermissions(
                this,
                arrayOf(Manifest.permission.READ_EXTERNAL_STORAGE, Manifest.permission.WRITE_EXTERNAL_STORAGE),
                100
            )
        }
    }

    private fun loadFiles(): List<File> {
        val dirs = mutableListOf<File>()
        
        if (customFolder != null) {
            dirs.add(customFolder!!)
        } else {
            dirs.addAll(listOf(
                File("/system/media"),
                File("/system/omc/VZW/res/media"),
                File("/odm/omc/VZW/res/media"),
                File("/product/omc/VZW/res/media")
            ))
        }

        val files = mutableListOf<File>()
        for (dir in dirs) {
            if (dir.exists() && dir.isDirectory) {
                dir.listFiles { _, name -> name.lowercase().endsWith(".spi") }?.let {
                    files.addAll(it)
                }
            }
        }
        return files.sortedBy { it.name }
    }

    private fun getPathFromUri(uri: Uri): String? {
        if (DocumentsContract.isTreeUri(uri)) {
            val treeId = DocumentsContract.getTreeDocumentId(uri)
            val split = treeId.split(":")
            if (split.size < 2) return null
            val type = split[0]
            val path = split[1]
            return if ("primary".equals(type, ignoreCase = true)) {
                Environment.getExternalStorageDirectory().toString() + "/" + path
            } else {
                "/storage/$type/$path"
            }
        }
        return null
    }

    private fun Float.spToPx(): Float = this * resources.displayMetrics.scaledDensity
}
