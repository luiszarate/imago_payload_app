package aero.imago.payloadcontrol.ui

import aero.imago.payloadcontrol.app.AppModel
import aero.imago.payloadcontrol.control.GamepadAction
import aero.imago.payloadcontrol.control.GamepadAxisFunction
import aero.imago.payloadcontrol.control.GamepadLearnTarget
import aero.imago.payloadcontrol.control.GamepadMapping
import aero.imago.payloadcontrol.control.buttonLabel
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.SportsEsports
import androidx.compose.material.icons.outlined.SportsEsports
import androidx.compose.material3.Icon
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.unit.dp

/** Controller mapping: pick a function, then press the button or move the axis for it. */
fun LazyListScope.gamepadSections(model: AppModel) {
    formSection(
        "controller", "Controller",
        "Xbox, PlayStation and generic Bluetooth or USB gamepads work, and HOTAS or flight sticks. Pair it in Android Settings › Bluetooth or plug it in by USB (an OTG adapter on older devices).",
    ) {
        FormRow {
            val name = model.gamepadName
            Icon(if (name == null) Icons.Outlined.SportsEsports else Icons.Filled.SportsEsports, contentDescription = null,
                 tint = if (name == null) Theme.textTertiary else Theme.ok, modifier = Modifier.size(22.dp))
            Text(name ?: "No controller connected", style = AppFont.body(15, 600), modifier = Modifier.padding(start = 10.dp))
        }
    }
    formSection(
        "axes", "Axes",
        "Assign, then move the stick, hat, throttle or trigger fully in the direction that should mean right, up or zoom in.",
    ) {
        GamepadAxisFunction.entries.forEachIndexed { index, function ->
            if (index > 0) RowDivider()
            AxisRow(function)
        }
    }
    formSection("buttons", "Buttons", "Assign, then press the button. A button does one thing: assigning it again moves it.") {
        GamepadAction.entries.forEachIndexed { index, action ->
            if (index > 0) RowDivider()
            ActionRow(action)
        }
    }
    formSection("reset") {
        FormRow(Modifier.clickable(role = Role.Button) {
            model.learnGamepadInput(null)
            model.updateGamepadMapping(GamepadMapping.standard)
        }) {
            Text("Restore default mapping", style = AppFont.body(15, 600, Theme.danger))
        }
    }
}

@Composable
private fun AxisRow(function: GamepadAxisFunction) {
    val model = LocalModel.current
    val binding = model.gamepadMapping.axes[function]
    val target = GamepadLearnTarget.Axis(function)
    val learning = model.gamepadLearning == target
    Column {
        FormRow {
            Column(Modifier.weight(1f)) {
                Text(function.title, style = AppFont.body(15, 600))
                Text(if (learning) "Move the axis now…" else binding?.label ?: "Not assigned",
                     style = AppFont.body(12, 500, if (learning) Theme.magenta else Theme.textSecondary))
            }
            RowButtons(
                learning = learning,
                canClear = binding != null,
                onAssign = { model.learnGamepadInput(if (learning) null else target) },
                onClear = { model.updateGamepadMapping(model.gamepadMapping.unbind(function)) },
            )
        }
        if (binding != null) {
            SwitchRow("Invert", binding.inverted) {
                model.updateGamepadMapping(model.gamepadMapping.withInverted(function, it))
            }
        }
    }
}

@Composable
private fun ActionRow(action: GamepadAction) {
    val model = LocalModel.current
    val keyCode = model.gamepadMapping.button(action)
    val target = GamepadLearnTarget.Action(action)
    val learning = model.gamepadLearning == target
    FormRow {
        Column(Modifier.weight(1f)) {
            Text(action.title, style = AppFont.body(15, 600))
            Text(if (learning) "Press a button now…" else keyCode?.let(::buttonLabel) ?: "Not assigned",
                 style = AppFont.body(12, 500, if (learning) Theme.magenta else Theme.textSecondary))
        }
        RowButtons(
            learning = learning,
            canClear = keyCode != null,
            onAssign = { model.learnGamepadInput(if (learning) null else target) },
            onClear = { model.updateGamepadMapping(model.gamepadMapping.unbind(action)) },
        )
    }
}

@Composable
private fun RowButtons(learning: Boolean, canClear: Boolean, onAssign: () -> Unit, onClear: () -> Unit) {
    val model = LocalModel.current
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
        if (canClear && !learning) {
            OutlinedButton(onClick = onClear) { Text("Clear", style = AppFont.body(13, 600, Theme.textSecondary)) }
        }
        OutlinedButton(onClick = onAssign, enabled = model.gamepadName != null || learning) {
            Text(if (learning) "Cancel" else "Assign", style = AppFont.body(13, 600, Theme.magenta))
        }
    }
}
