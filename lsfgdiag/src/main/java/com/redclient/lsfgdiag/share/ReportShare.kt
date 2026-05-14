package com.redclient.lsfgdiag.share

import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.core.content.FileProvider
import com.redclient.lsfgdiag.data.Report
import java.io.File

/**
 * Writes a Report to the app's private files dir and exposes it via the
 * FileProvider authority declared in AndroidManifest. Caller can then chain
 * an ACTION_SEND with the returned content:// URI.
 */
object ReportShare {

    private const val SUBDIR = "reports"
    private const val MIME = "text/plain"

    /**
     * Persist [report] to disk under filesDir/reports/ and return both the
     * resulting File and a content:// URI suitable for ACTION_SEND.
     *
     * The file is overwritten on every save by design: callers usually only
     * want the latest snapshot, and we don't want this app to leak storage
     * across sessions on devices that already have low free space.
     */
    fun saveAndPrepareUri(ctx: Context, report: Report): Pair<File, Uri> {
        val dir = File(ctx.filesDir, SUBDIR).apply { mkdirs() }
        val file = File(dir, report.suggestedFilename())
        file.writeText(report.toPlainText())
        val uri = FileProvider.getUriForFile(
            ctx,
            "${ctx.packageName}.fileprovider",
            file,
        )
        return file to uri
    }

    /**
     * Launches the system Share sheet with the report attached. Returns false
     * if no app on the device can handle text sharing (rare but possible
     * on stripped-down OEM builds).
     */
    fun share(ctx: Context, report: Report): Boolean {
        val (_, uri) = saveAndPrepareUri(ctx, report)
        val send = Intent(Intent.ACTION_SEND).apply {
            type = MIME
            putExtra(Intent.EXTRA_STREAM, uri)
            putExtra(Intent.EXTRA_SUBJECT, "LsfgDiag report — ${android.os.Build.MODEL}")
            // Short body so the report itself is the focus.
            putExtra(Intent.EXTRA_TEXT,
                "LsfgDiag diagnostic report for ${android.os.Build.MANUFACTURER} ${android.os.Build.MODEL}.")
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }
        val chooser = Intent.createChooser(send, "Share LsfgDiag report")
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        return try {
            ctx.startActivity(chooser)
            true
        } catch (_: ActivityNotFoundException) {
            false
        }
    }
}
