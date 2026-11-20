package aero.imago.payloadcontrol.ui

import aero.imago.payloadcontrol.core.WebIsotherm
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Remove
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.scale
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/**
 * The isotherm of the payload's web settings page (our Orus L has it only there), from the
 * thermal bar's ISO: one threshold, everything warmer is coloured. On/off and the presets go at
 * once; a threshold of one's own goes with Apply. Until the page has answered it says why
 * nothing can be set.
 */
@Composable
fun WebIsothermPanel() {
    val model = LocalModel.current
    val web = model.webIsotherm
    var draft by remember { mutableStateOf<Int?>(null) }
    val threshold = draft ?: web?.thresholdC ?: 35
    val known = web != null
    val limits = WebIsotherm.THRESHOLD_LIMITS
    Column(Modifier.width(340.dp).padding(horizontal = 16.dp, vertical = 8.dp),
           verticalArrangement = Arrangement.spacedBy(14.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("ISOTHERM", style = AppFont.display(13, 700, tracking = 1.2.sp), modifier = Modifier.weight(1f))
            Switch(
                checked = web?.enabled == true,
                onCheckedChange = { model.setWebIsotherm(on = it) },
                enabled = known,
                colors = SwitchDefaults.colors(checkedTrackColor = Theme.magenta, checkedThumbColor = Theme.text),
                modifier = Modifier.semantics { contentDescription = "Isotherm" },
            )
        }
        Text(
            when {
                known -> "The payload's own isotherm: everything warmer than the threshold is coloured. Saved on its web page; it shows at once."
                model.webIsothermFailed -> "The payload's web page does not answer (the payload IP in the profile, port 8000). The app keeps trying."
                else -> "Reading the payload's web page…"
            },
            style = AppFont.body(12, 500, if (!known && model.webIsothermFailed) Theme.warning else Theme.textSecondary),
        )

        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            for ((presetC, name) in WebIsotherm.PRESETS) {
                val lit = web != null && web.enabled && web.thresholdC == presetC
                val source = remember { MutableInteractionSource() }
                Column(
                    Modifier
                        .weight(1f)
                        .scale(rememberPressScale(source))
                        .heightIn(min = 46.dp)
                        .fill(lit, RoundedCornerShape(11.dp))
                        .clickable(interactionSource = source, indication = null, enabled = known, role = Role.Button) {
                            draft = presetC
                            model.setWebIsotherm(on = true, thresholdC = presetC)
                        }
                        .semantics { contentDescription = "$name, $presetC degrees" }
                        .padding(vertical = 6.dp),
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.Center,
                ) {
                    Text(name, style = AppFont.display(12, 700, if (known) Theme.text else Theme.textTertiary), maxLines = 1)
                    Text("$presetC °C", style = AppFont.figures(10, 500, if (lit) Theme.text else Theme.textSecondary),
                         maxLines = 1)
                }
            }
        }

        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("Threshold", style = AppFont.body(14, 600, Theme.textSecondary), modifier = Modifier.weight(1f))
            RepeatButton(Icons.Filled.Remove, "Threshold lower", known && threshold > limits.first) {
                draft = maxOf(limits.first, (draft ?: threshold) - 1)
            }
            Text(if (known) "$threshold °C" else "— °C", style = AppFont.figures(16, 600), maxLines = 1,
                 textAlign = TextAlign.Center, modifier = Modifier.width(80.dp))
            RepeatButton(Icons.Filled.Add, "Threshold higher", known && threshold < limits.last) {
                draft = minOf(limits.last, (draft ?: threshold) + 1)
            }
        }
        if (threshold > WebIsotherm.LOW_RANGE_MAX_C) {
            Text("Over 150 °C the camera measures −20 to 550 °C.", style = AppFont.body(11, 500, Theme.textSecondary))
        }

        val source = remember { MutableInteractionSource() }
        Box(
            Modifier
                .scale(rememberPressScale(source))
                .alpha(if (known) 1f else 0.4f)
                .fillMaxWidth()
                .heightIn(min = 46.dp)
                .background(Theme.accent, RoundedCornerShape(12.dp))
                .clickable(interactionSource = source, indication = null, enabled = known, role = Role.Button) {
                    model.setWebIsotherm(on = true, thresholdC = threshold)
                },
            contentAlignment = Alignment.Center,
        ) {
            Text(if (web?.enabled == true) "Apply $threshold °C" else "Turn on $threshold °C",
                 style = AppFont.display(14, 700))
        }
    }
}
