package com.redclient.lsfgdiag.ui.theme

import android.os.Build
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext

private val DiagBlue = Color(0xFF0277BD)
private val DiagAmber = Color(0xFFFFA000)
private val DiagRed = Color(0xFFD32F2F)

private val LightColors = lightColorScheme(
    primary = DiagBlue,
    onPrimary = Color.White,
    secondary = DiagAmber,
    error = DiagRed,
)

private val DarkColors = darkColorScheme(
    primary = DiagBlue,
    onPrimary = Color.White,
    secondary = DiagAmber,
    error = DiagRed,
)

@Composable
fun LsfgDiagTheme(
    darkTheme: Boolean = isSystemInDarkTheme(),
    dynamicColor: Boolean = true,
    content: @Composable () -> Unit,
) {
    val scheme = when {
        dynamicColor && Build.VERSION.SDK_INT >= Build.VERSION_CODES.S -> {
            val ctx = LocalContext.current
            if (darkTheme) dynamicDarkColorScheme(ctx) else dynamicLightColorScheme(ctx)
        }
        darkTheme -> DarkColors
        else -> LightColors
    }
    MaterialTheme(colorScheme = scheme, content = content)
}
