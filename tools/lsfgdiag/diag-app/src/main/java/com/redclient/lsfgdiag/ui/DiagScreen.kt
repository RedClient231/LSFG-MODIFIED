package com.redclient.lsfgdiag.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.IosShare
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Snackbar
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DiagScreen(viewModel: DiagViewModel) {
    val state by viewModel.state.collectAsState()
    val snackbarHostState = remember { SnackbarHostState() }

    LaunchedEffect(state.message) {
        state.message?.let {
            snackbarHostState.showSnackbar(it)
            viewModel.dismissMessage()
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("LsfgDiag") },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = MaterialTheme.colorScheme.primary,
                    titleContentColor = Color.White,
                    actionIconContentColor = Color.White,
                ),
            )
        },
        snackbarHost = { SnackbarHost(snackbarHostState) },
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .verticalScroll(rememberScrollState())
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            IntroBlock()

            Button(
                onClick = viewModel::saveAndShare,
                modifier = Modifier.fillMaxWidth(),
                colors = ButtonDefaults.buttonColors(),
            ) {
                Icon(Icons.Filled.IosShare, contentDescription = null)
                Spacer(Modifier.width(8.dp))
                Text("Save & Share full report")
            }

            state.lastSavedPath?.let { path ->
                Text(
                    "Last saved: $path",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }

            Section(
                title = "Device",
                content = state.deviceInfo,
                isLoading = state.deviceInfo == null,
                onRefresh = viewModel::loadDeviceInfo,
            )
            Section(
                title = "Vulkan capabilities",
                content = state.vulkan,
                isLoading = state.isVulkanRunning,
                onRefresh = viewModel::runVulkanProbe,
            )
            Section(
                title = "Privileged capture API",
                content = state.capture,
                isLoading = state.capture == null,
                onRefresh = viewModel::loadCaptureProbe,
            )
            Section(
                title = "Logcat (recent)",
                content = state.logcat,
                isLoading = state.isLogcatRunning,
                onRefresh = viewModel::refreshLogcat,
                maxPreviewLines = 30,
            )

            Spacer(Modifier.height(48.dp))
        }
    }
}

@Composable
private fun IntroBlock() {
    Card(colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.tertiaryContainer)) {
        Column(Modifier.padding(16.dp)) {
            Text(
                "Why this app exists",
                style = MaterialTheme.typography.titleMedium,
            )
            Spacer(Modifier.height(4.dp))
            Text(
                "LSFG-Android targets Adreno 7xx-class GPUs. Mali / MediaTek devices " +
                    "(Helio G99 etc.) run into driver-specific issues. This tool dumps " +
                    "the four facts that matter — your Vulkan capabilities, the captureDisplay " +
                    "reflection chain, recent logcat, and device build info — into a single " +
                    "shareable text file. Send that file to whoever is helping you and they " +
                    "can pinpoint exactly which incompatibility you're hitting.",
                style = MaterialTheme.typography.bodySmall,
            )
            Spacer(Modifier.height(8.dp))
            Text(
                "If logcat looks empty:",
                style = MaterialTheme.typography.titleSmall,
            )
            Text(
                "Connect the phone to a PC with ADB and run\n  adb shell pm grant com.redclient.lsfgdiag android.permission.READ_LOGS\n" +
                    "then re-open this app. Without that grant, Android user builds only let an " +
                    "app read its OWN logs — and this app doesn't generate any.",
                style = MaterialTheme.typography.bodySmall,
                fontFamily = FontFamily.Monospace,
                fontSize = 11.sp,
            )
        }
    }
}

@Composable
private fun Section(
    title: String,
    content: String?,
    isLoading: Boolean,
    onRefresh: () -> Unit,
    maxPreviewLines: Int = Int.MAX_VALUE,
) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(title, style = MaterialTheme.typography.titleMedium)
                if (isLoading) {
                    Spacer(Modifier.width(8.dp))
                    CircularProgressIndicator(modifier = Modifier.size(16.dp), strokeWidth = 2.dp)
                }
                Spacer(Modifier.weight(1f))
                IconButton(onClick = onRefresh, enabled = !isLoading) {
                    Icon(Icons.Filled.Refresh, contentDescription = "Refresh $title")
                }
            }
            Spacer(Modifier.height(8.dp))
            if (content == null) {
                Text("Running...", style = MaterialTheme.typography.bodySmall)
            } else {
                MonoBlock(content = content, maxPreviewLines = maxPreviewLines)
            }
        }
    }
}

@Composable
private fun MonoBlock(content: String, maxPreviewLines: Int) {
    val truncated = if (maxPreviewLines == Int.MAX_VALUE) {
        content
    } else {
        val lines = content.lineSequence().take(maxPreviewLines).toList()
        val total = content.count { it == '\n' } + 1
        if (lines.size < total) {
            lines.joinToString("\n") + "\n... (${total - lines.size} more line(s) — included in shared report)"
        } else {
            content
        }
    }
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .background(Color(0xFF263238), RoundedCornerShape(8.dp))
            .padding(10.dp)
            .horizontalScroll(rememberScrollState()),
    ) {
        Text(
            text = truncated,
            color = Color(0xFFEEFFEE),
            fontFamily = FontFamily.Monospace,
            fontSize = 11.sp,
            fontWeight = FontWeight.Normal,
        )
    }
}
