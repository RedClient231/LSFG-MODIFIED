package com.redclient.lsfgdiag.ui

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.redclient.lsfgdiag.data.DeviceInfoCollector
import com.redclient.lsfgdiag.data.LogcatGrabber
import com.redclient.lsfgdiag.data.PrivilegedCaptureProbe
import com.redclient.lsfgdiag.data.Report
import com.redclient.lsfgdiag.data.VulkanProbe
import com.redclient.lsfgdiag.share.ReportShare
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * One-screen state container. Each probe is independently re-runnable so the
 * user can refresh logcat after triggering an LSFG glitch without re-running
 * the (slow) Vulkan probe.
 */
data class DiagUiState(
    val deviceInfo: String? = null,
    val vulkan: String? = null,
    val capture: String? = null,
    val logcat: String? = null,
    val isVulkanRunning: Boolean = false,
    val isLogcatRunning: Boolean = false,
    val lastSavedPath: String? = null,
    val message: String? = null,
)

class DiagViewModel(app: Application) : AndroidViewModel(app) {

    private val _state = MutableStateFlow(DiagUiState())
    val state: StateFlow<DiagUiState> = _state.asStateFlow()

    init {
        // Kick off the cheap probes immediately on first launch so the user
        // sees populated cards as soon as the screen renders. The Vulkan
        // probe is slow enough on some devices (~200 ms) that we still mark
        // it isVulkanRunning during the first launch for clarity.
        loadDeviceInfo()
        loadCaptureProbe()
        runVulkanProbe()
        refreshLogcat()
    }

    fun loadDeviceInfo() {
        viewModelScope.launch {
            val s = withContext(Dispatchers.IO) {
                DeviceInfoCollector.collect(getApplication())
            }
            _state.update { it.copy(deviceInfo = s) }
        }
    }

    fun loadCaptureProbe() {
        viewModelScope.launch {
            val s = withContext(Dispatchers.Default) { PrivilegedCaptureProbe.runProbe() }
            _state.update { it.copy(capture = s) }
        }
    }

    fun runVulkanProbe() {
        if (_state.value.isVulkanRunning) return
        _state.update { it.copy(isVulkanRunning = true) }
        viewModelScope.launch {
            val s = withContext(Dispatchers.Default) { VulkanProbe.runProbe() }
            _state.update { it.copy(vulkan = s, isVulkanRunning = false) }
        }
    }

    fun refreshLogcat() {
        if (_state.value.isLogcatRunning) return
        _state.update { it.copy(isLogcatRunning = true) }
        viewModelScope.launch {
            val s = withContext(Dispatchers.IO) { LogcatGrabber.grab(maxLines = 5000) }
            _state.update { it.copy(logcat = s, isLogcatRunning = false) }
        }
    }

    fun saveAndShare() {
        viewModelScope.launch {
            val ctx = getApplication<Application>().applicationContext
            val report = withContext(Dispatchers.IO) {
                // If the user hasn't waited for individual probes, force-build
                // a fresh report end-to-end here. Cheaper than coordinating
                // partial state with the share intent.
                Report.build(ctx)
            }
            val (file, _) = withContext(Dispatchers.IO) {
                com.redclient.lsfgdiag.share.ReportShare.saveAndPrepareUri(ctx, report)
            }
            _state.update { it.copy(lastSavedPath = file.absolutePath, message = "Saved report. Tap Share to send it.") }
            // Auto-pop the share sheet — this is what the user typically wants.
            withContext(Dispatchers.Main) {
                val ok = ReportShare.share(ctx, report)
                if (!ok) {
                    _state.update {
                        it.copy(message = "No app on this device can share text/plain. Report saved at: ${file.absolutePath}")
                    }
                }
            }
        }
    }

    fun dismissMessage() {
        _state.update { it.copy(message = null) }
    }
}
