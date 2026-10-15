package aero.imago.payloadcontrol.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.displayCutout
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.ime
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.union
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.UnfoldMore
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.Icon
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp

// Building blocks of the settings and connection screens: full-screen pages drawn in the app's
// own window (not dialogs), so a gamepad keeps reaching the activity while one is open.

/** A full-screen page with a title bar. */
@Composable
fun FormPage(
    title: String,
    leading: (@Composable RowScope.() -> Unit)? = null,
    trailing: @Composable RowScope.() -> Unit,
    header: (@Composable ColumnScope.() -> Unit)? = null,
    content: LazyListScope.() -> Unit,
) {
    Column(
        Modifier
            .fillMaxSize()
            .background(Theme.background)
            .blockTouches()
            .windowInsetsPadding(WindowInsets.displayCutout.only(WindowInsetsSides.Horizontal).union(WindowInsets.ime)),
    ) {
        Row(Modifier.fillMaxWidth().height(52.dp).padding(horizontal = 8.dp), verticalAlignment = Alignment.CenterVertically) {
            Row(Modifier.weight(1f), content = leading ?: {})
            Text(title, style = AppFont.display(17, 600), maxLines = 1, overflow = TextOverflow.Ellipsis)
            Row(Modifier.weight(1f), horizontalArrangement = Arrangement.End, content = trailing)
        }
        Box(Modifier.fillMaxWidth().height(1.dp).background(Theme.stroke))
        Box(Modifier.fillMaxSize(), contentAlignment = Alignment.TopCenter) {
            LazyColumn(
                Modifier.widthIn(max = 720.dp).fillMaxSize(),
                contentPadding = PaddingValues(horizontal = 16.dp, vertical = 12.dp),
                verticalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                if (header != null) item { Column(content = header) }
                content()
            }
        }
    }
}

@Composable
fun BarButton(text: String, enabled: Boolean = true, bold: Boolean = false, onClick: () -> Unit) {
    TextButton(onClick = onClick, enabled = enabled) {
        Text(text, style = AppFont.body(16, if (bold) 700 else 600, if (enabled) Theme.magenta else Theme.textTertiary))
    }
}

/** A group of rows with an optional header and footer, like an iOS form section. */
fun LazyListScope.formSection(
    key: String,
    header: String? = null,
    footer: String? = null,
    rows: @Composable ColumnScope.() -> Unit,
) {
    item(key = key) {
        Column(Modifier.padding(top = 10.dp)) {
            if (header != null) {
                Text(header.uppercase(), style = AppFont.display(12, 600, Theme.textSecondary),
                     modifier = Modifier.padding(start = 14.dp, bottom = 6.dp))
            }
            Column(Modifier.fillMaxWidth().background(Theme.surfaceRaised, RoundedCornerShape(12.dp)), content = rows)
            if (footer != null) {
                Text(footer, style = AppFont.body(12, 500, Theme.textSecondary),
                     modifier = Modifier.padding(start = 14.dp, end = 14.dp, top = 6.dp))
            }
        }
    }
}

/** One row inside a section. */
@Composable
fun FormRow(modifier: Modifier = Modifier, content: @Composable RowScope.() -> Unit) {
    Row(
        modifier.fillMaxWidth().heightIn(min = 48.dp).padding(horizontal = 14.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
        content = content,
    )
}

@Composable
fun RowDivider() {
    Box(Modifier.fillMaxWidth().padding(start = 14.dp).height(1.dp).background(Theme.stroke))
}

@Composable
fun SwitchRow(title: String, checked: Boolean, note: String? = null, enabled: Boolean = true,
              titleExtra: (@Composable () -> Unit)? = null, onChange: (Boolean) -> Unit) {
    FormRow(Modifier.clickable(enabled = enabled, role = Role.Switch) { onChange(!checked) }) {
        Column(Modifier.weight(1f)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(title, style = AppFont.body(15, 600))
                titleExtra?.invoke()
            }
            if (note != null) Text(note, style = AppFont.body(12, 500, Theme.textTertiary))
        }
        Switch(
            checked = checked, onCheckedChange = onChange, enabled = enabled,
            colors = SwitchDefaults.colors(checkedTrackColor = Theme.magenta, checkedThumbColor = Theme.text),
        )
    }
}

/** A row that opens a menu of choices. */
@Composable
fun <T> ChoiceRow(
    title: String,
    selected: T?,
    options: List<Pair<T, String>>,
    note: String? = null,
    titleExtra: (@Composable () -> Unit)? = null,
    onSelect: (T) -> Unit,
) {
    var open by remember { mutableStateOf(false) }
    Box {
        FormRow(Modifier.clickable(role = Role.Button) { open = true }) {
            Column(Modifier.weight(1f)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(title, style = AppFont.body(15, 600))
                    titleExtra?.invoke()
                }
                if (note != null) Text(note, style = AppFont.body(12, 500, Theme.textTertiary))
            }
            Text(options.firstOrNull { it.first == selected }?.second ?: "—",
                 style = AppFont.body(15, 600, Theme.textSecondary), maxLines = 1)
            Icon(Icons.Filled.UnfoldMore, contentDescription = null, tint = Theme.textTertiary,
                 modifier = Modifier.padding(start = 4.dp).size(18.dp))
        }
        DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
            for ((value, label) in options) {
                MenuItem(label, value == selected) {
                    open = false
                    onSelect(value)
                }
            }
        }
    }
}

@Composable
fun SliderRow(
    title: String,
    valueText: String,
    value: Float,
    range: ClosedFloatingPointRange<Float>,
    step: Float,
    minLabel: String? = null,
    maxLabel: String? = null,
    onChange: (Float) -> Unit,
) {
    Column(Modifier.fillMaxWidth().padding(horizontal = 14.dp, vertical = 8.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(title, style = AppFont.body(15, 600), modifier = Modifier.weight(1f))
            Text(valueText, style = AppFont.figures(14, 600, Theme.textSecondary))
        }
        Row(verticalAlignment = Alignment.CenterVertically) {
            if (minLabel != null) Text(minLabel, style = AppFont.body(11, 500, Theme.textSecondary))
            val steps = ((range.endInclusive - range.start) / step).toInt() - 1
            Slider(
                value = value, onValueChange = onChange, valueRange = range, steps = steps.coerceIn(0, 200),
                modifier = Modifier.weight(1f).padding(horizontal = 6.dp),
                colors = SliderDefaults.colors(thumbColor = Theme.text, activeTrackColor = Theme.magenta,
                                               activeTickColor = Theme.magenta.copy(alpha = 0f),
                                               inactiveTickColor = Theme.text.copy(alpha = 0f)),
            )
            if (maxLabel != null) Text(maxLabel, style = AppFont.body(11, 500, Theme.textSecondary))
        }
    }
}
