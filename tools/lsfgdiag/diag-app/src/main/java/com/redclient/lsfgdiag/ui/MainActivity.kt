package com.redclient.lsfgdiag.ui

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.Surface
import androidx.compose.ui.Modifier
import com.redclient.lsfgdiag.ui.theme.LsfgDiagTheme

class MainActivity : ComponentActivity() {

    private val viewModel: DiagViewModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            LsfgDiagTheme {
                Surface(modifier = Modifier.fillMaxSize()) {
                    DiagScreen(viewModel = viewModel)
                }
            }
        }
    }
}
