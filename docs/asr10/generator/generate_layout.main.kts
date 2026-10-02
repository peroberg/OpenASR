#!/usr/bin/env kotlin

import java.io.File

// =====================================================================
// KONFIGURATION: FÖRHANDSGRANSKNING AV ANNUNCIATORS
// =====================================================================
val PREVIEW_ALL_ANNUNCIATORS = true

// =====================================================================
// 1. TYPSÄKER XML-BYGGARE FÖR MAME
// =====================================================================
class XmlTag(
	val name: String,
) {
	val attrs = linkedMapOf<String, Any>()
	val children = mutableListOf<XmlTag>()
	var textContent: String? = null

	fun attr(
		k: String,
		v: Any,
	) {
		attrs[k] = v
	}

	fun tag(
		name: String,
		block: XmlTag.() -> Unit = {},
	): XmlTag {
		val child = XmlTag(name).apply(block)
		children.add(child)
		return child
	}

	fun render(indent: String = ""): String =
		buildString {
			append("$indent<$name")
			attrs.forEach { (k, v) -> append(" $k=\"$v\"") }
			if (children.isEmpty() && textContent == null) {
				append(" />\n")
			} else if (children.isEmpty() && textContent != null) {
				append(">$textContent</$name>\n")
			} else {
				append(">\n")
				textContent?.let { append("$indent\t$it\n") }
				children.forEach { append(it.render("$indent\t")) }
				append("$indent</$name>\n")
			}
		}
}

fun xml(
	root: String,
	block: XmlTag.() -> Unit,
): String = "<?xml version=\"1.0\"?>\n<!-- license:CC0-1.0 -->\n" + XmlTag(root).apply(block).render()

// Konvenansmetoder för MAME XML
fun XmlTag.color(c: Color) =
	tag("color") {
		attr("red", c.r)
		attr("green", c.g)
		attr("blue", c.b)
	}

fun XmlTag.bounds(b: Rect) =
	tag("bounds") {
		attr("x", b.x)
		attr("y", b.y)
		attr("width", b.w)
		attr("height", b.h)
	}

fun XmlTag.bounds(
	x: Number,
	y: Number,
	w: Number,
	h: Number,
) = tag("bounds") {
	attr("x", x)
	attr("y", y)
	attr("width", w)
	attr("height", h)
}

fun XmlTag.item(
	ref: String,
	b: Rect,
	block: XmlTag.() -> Unit = {},
) = tag("element") {
	attr("ref", ref)
	bounds(b)
	block()
}

fun XmlTag.rect(
	state: String? = null,
	x: Number? = null,
	y: Number? = null,
	w: Number? = null,
	h: Number? = null,
	c: Color? = null,
) = tag("rect") {
	state?.let { attr("state", it) }
	if (x != null && y != null && w != null && h != null) bounds(x, y, w, h)
	c?.let { color(it) }
}

fun XmlTag.viewItem(
	ref: String,
	b: Rect,
	name: String? = null,
	inputTag: String? = null,
	inputMask: String? = null,
	clickthrough: String? = null,
) = tag("element") {
	name?.let { attr("name", it) }
	attr("ref", ref)
	inputTag?.let { attr("inputtag", it) }
	inputMask?.let { attr("inputmask", it) }
	clickthrough?.let { attr("clickthrough", it) }
	bounds(b)
}

// =====================================================================
// 2. MÅTTENHETER, GEOMETRI & DEKLARATIVT LAYOUT-DSL
// =====================================================================
val PANEL_WIDTH = 226.0
val PANEL_HEIGHT = 138.0
val BASE_BUTTON_W = 12.0
val BASE_BUTTON_H = 6.0

val Double.bw: Double get() = this * BASE_BUTTON_W

data class Point(
	val x: Double,
	val y: Double,
)

data class Size(
	val w: Double,
	val h: Double,
)

interface Anchorable {
	val structuralBounds: Rect
	val visualBounds: Rect get() = structuralBounds

	val centerX: Double get() = structuralBounds.cx
	val centerY: Double get() = structuralBounds.cy
	val left: Double get() = structuralBounds.x
	val right: Double get() = structuralBounds.right
	val top: Double get() = structuralBounds.y
	val bottom: Double get() = structuralBounds.bottom

	fun midpointTo(other: Anchorable): Point = Point((centerX + other.centerX) / 2.0, (centerY + other.centerY) / 2.0)
}

interface Bounded : Anchorable {
	val bounds: Rect get() = visualBounds
}

interface Component : Bounded {
	fun lower(): List<Primitive>
}

data class Rect(
	val x: Double,
	val y: Double,
	val w: Double,
	val h: Double,
) : Anchorable {
	override val structuralBounds: Rect get() = this
	override val visualBounds: Rect get() = this
	val cx get() = x + w / 2.0
	val cy get() = y + h / 2.0
	override val left: Double get() = x
	override val right: Double get() = x + w
	override val top: Double get() = y
	override val bottom: Double get() = y + h

	companion object {
		fun fromCenter(
			center: Point,
			size: Size,
		) = Rect(center.x - size.w / 2.0, center.y - size.h / 2.0, size.w, size.h)

		fun fromCenter(
			cx: Double,
			cy: Double,
			w: Double,
			h: Double,
		) = Rect(cx - w / 2.0, cy - h / 2.0, w, h)
	}

	fun intersects(
		other: Rect,
		tolerance: Double = 0.05,
	): Boolean =
		this.x < other.right - tolerance && this.right > other.x + tolerance &&
			this.y < other.bottom - tolerance && this.bottom > other.y + tolerance
}

fun Collection<Rect>.boundingRect(): Rect {
	if (isEmpty()) return Rect(0.0, 0.0, 0.0, 0.0)
	val minX = minOf { it.x }
	val minY = minOf { it.y }
	val maxX = maxOf { it.right }
	val maxY = maxOf { it.bottom }
	return Rect(minX, minY, maxX - minX, maxY - minY)
}

val Collection<Component>.combinedBounds: Rect get() = map { it.visualBounds }.boundingRect()

data class Span(
	val start: Double,
	val end: Double,
) {
	val length: Double get() = end - start

	init {
		require(end > start) { "Span ogiltig: end ($end) måste vara > start ($start)" }
	}

	fun assertFits(
		count: Int,
		elementSize: Double,
	): Boolean {
		val fits = length >= (count * elementSize) - 0.001
		if (!fits) {
			println(
				"⚠️  LAYOUT-VARNING: Span överskriden: $count element à $elementSize = ${count * elementSize} > span ($length)",
			)
		}
		return fits
	}

	fun distribute(
		count: Int,
		elementSize: Double,
	): List<Double> {
		assertFits(count, elementSize)
		if (count == 1) return listOf(start + length / 2.0)
		val gap = (length - (count * elementSize)) / (count - 1)
		val halfElem = elementSize / 2.0
		return (0 until count).map { i -> start + halfElem + i * (elementSize + gap) }
	}
}

data class Region(
	val x: Double,
	val y: Double,
	val w: Double,
	val h: Double,
	val label: String = "",
) : Anchorable {
	override val structuralBounds: Rect get() = Rect(x, y, w, h)
	override val visualBounds: Rect get() = structuralBounds
	override val left: Double get() = x
	override val right: Double get() = x + w
	override val top: Double get() = y
	override val bottom: Double get() = y + h
	override val centerX: Double get() = x + w / 2.0
	override val centerY: Double get() = y + h / 2.0

	val hSpan get() = Span(left, right)
	val vSpan get() = Span(top, bottom)

	fun splitCols(vararg ratios: Double): List<Region> {
		val total = ratios.sum()
		var curX = x
		return ratios.mapIndexed { idx, r ->
			val colW = w * (r / total)
			val reg = Region(curX, y, colW, h, label = "$label-col$idx")
			curX += colW
			reg
		}
	}

	fun splitRows(vararg ratios: Double): List<Region> {
		val total = ratios.sum()
		var curY = y
		return ratios.mapIndexed { idx, r ->
			val rowH = h * (r / total)
			val reg = Region(x, curY, w, rowH, label = "$label-row$idx")
			curY += rowH
			reg
		}
	}

	fun inset(
		dx: Double,
		dy: Double,
	) = Region(x + dx, y + dy, w - dx * 2, h - dy * 2, label)
}

// =====================================================================
// 3. FÄRGER & TEMALAGER
// =====================================================================
data class Color(
	val r: Double,
	val g: Double,
	val b: Double,
) {
	val hex get() = "#%02x%02x%02x".format((r * 255).toInt(), (g * 255).toInt(), (b * 255).toInt())

	companion object {
		val Bg = Color(0.12, 0.12, 0.12)
		val PanelFrame = Color(0.10, 0.10, 0.11)
		val TextMain = Color(0.90, 0.90, 0.90)
		val TextDim = Color(0.65, 0.65, 0.65)
		val LedRed = Color(1.0, 0.1, 0.1)
		val LedYellow = Color(1.0, 0.8, 0.1)
		val LedGreen = Color(0.1, 0.9, 0.2)
		val VfdGlow = Color(0.15, 0.95, 0.90)
		val VfdDim = Color(0.05, 0.25, 0.25)
		val VfdBg = Color(0.02, 0.05, 0.05)
		val SilkscreenLine = Color(0.35, 0.35, 0.38)
		val Filament = Color(0.40, 0.15, 0.05)
	}
}

data class SynthPalette(
	val bg: Color = Color.Bg,
	val panelFrame: Color = Color.PanelFrame,
	val textMain: Color = Color.TextMain,
	val textDim: Color = Color.TextDim,
	val ledRed: Color = Color.LedRed,
	val ledYellow: Color = Color.LedYellow,
	val ledGreen: Color = Color.LedGreen,
	val vfdGlow: Color = Color.VfdGlow,
	val vfdDim: Color = Color.VfdDim,
	val vfdBg: Color = Color.VfdBg,
	val silkscreenLine: Color = Color.SilkscreenLine,
	val filament: Color = Color.Filament,
)

data class ButtonTemplate(
	val id: String,
	val outerRim: Color = Color(0.0, 0.0, 0.0),
	val bodyBevel: Color = Color(0.10, 0.10, 0.10),
	val bodyReleased: Color = Color(0.02, 0.02, 0.02),
	val bodyPressed: Color = Color(0.35, 0.55, 0.70),
)

data class ButtonArchetype(
	val widthUnits: Double = 1.0,
	val heightRatio: Double,
	val template: ButtonTemplate,
) {
	val size: Size get() = Size(widthUnits.bw, widthUnits.bw * heightRatio)
	val height: Double get() = size.h
}

data class LedTemplate(
	val id: String,
	val onColor: Color,
	val offColor: Color,
	val cssHex: String,
)

data class SynthTheme(
	val palette: SynthPalette = SynthPalette(),
	val stdButton: ButtonTemplate = ButtonTemplate("btn_std"),
	val highButton: ButtonTemplate = ButtonTemplate("btn_high"),
	val redLed: LedTemplate = LedTemplate("led_red", Color.LedRed, Color(0.16, 0.03, 0.03), "#ff3326"),
	val yellowLed: LedTemplate = LedTemplate("led_yellow", Color.LedYellow, Color(0.15, 0.11, 0.02), "#ffd926"),
	val greenLed: LedTemplate = LedTemplate("led_green", Color.LedGreen, Color(0.02, 0.14, 0.03), "#40ff40"),
) {
	val stdButtonArchetype = ButtonArchetype(1.0, 0.50, stdButton)
	val highButtonArchetype = ButtonArchetype(1.0, 9.0 / 12.0, highButton)
}

val theme = SynthTheme()

// =====================================================================
// 4. GRAFISKA PRIMITIVER & FONTER
// =====================================================================
data class HardwareBinding(
	val tag: String,
	val mask: String,
)

sealed interface Primitive : Component {
	override val bounds: Rect
	override val structuralBounds: Rect get() = bounds
	override val visualBounds: Rect get() = bounds

	override fun lower(): List<Primitive> = listOf(this)

	data class ButtonInstance(
		override val bounds: Rect,
		val template: ButtonTemplate,
		val binding: HardwareBinding? = null,
	) : Primitive

	data class LedInstance(
		override val bounds: Rect,
		val signalName: String,
		val template: LedTemplate,
	) : Primitive

	data class Text(
		override val bounds: Rect,
		val content: String,
		val color: Color,
		val size: Double,
		val alignCenter: Boolean = true,
	) : Primitive

	data class PanelFrame(
		override val bounds: Rect,
		val fill: Color,
	) : Primitive

	data class SliderHandle(
		override val bounds: Rect,
		val minY: Double,
		val maxY: Double,
		val tag: String,
		val mask: String,
	) : Primitive

	data class Vfd14Digit(
		override val bounds: Rect,
		val digitIdx: Int,
		val underlineIdx: Int,
		val rawMask: Int,
		val underlineLit: Boolean = false,
	) : Primitive

	data class AnnunciatorWord(
		override val bounds: Rect,
		val text: String,
		val hasFrame: Boolean,
		val bitIdx: Int? = null,
		val isLit: Boolean = false,
	) : Primitive

	data class FilamentLine(
		override val bounds: Rect,
		val color: Color,
	) : Primitive
}

object Vfd14Font {
	const val A = 1 shl 0
	const val B = 1 shl 1
	const val C = 1 shl 2
	const val D = 1 shl 3
	const val E = 1 shl 4
	const val F = 1 shl 5
	const val G1 = 1 shl 6
	const val G2 = 1 shl 7
	const val H = 1 shl 8
	const val J = 1 shl 9
	const val K = 1 shl 10
	const val L = 1 shl 11
	const val M = 1 shl 12
	const val N = 1 shl 13
	const val DP = 1 shl 14
	const val COMMA = 1 shl 15

	val table =
		mapOf(
			' ' to 0,
			'A' to (A or B or C or E or F or G1 or G2),
			'B' to (A or B or C or D or J or M or G2),
			'C' to (A or D or E or F),
			'D' to (A or B or C or D or J or M),
			'E' to (A or D or E or F or G1),
			'F' to (A or E or F or G1),
			'G' to (A or C or D or E or F or G2),
			'H' to (B or C or E or F or G1 or G2),
			'I' to (J or M),
			'J' to (B or C or D or E),
			'K' to (E or F or G1 or K or N),
			'L' to (D or E or F),
			'M' to (B or C or E or F or H or K),
			'N' to (B or C or E or F or H or N),
			'O' to (A or B or C or D or E or F),
			'P' to (A or B or E or F or G1 or G2),
			'Q' to (A or B or C or D or E or F or N),
			'R' to (A or B or E or F or G1 or G2 or N),
			'S' to (A or C or D or F or G1 or G2),
			'T' to (A or J or M),
			'U' to (B or C or D or E or F),
			'V' to (E or F or L or K),
			'W' to (B or C or E or F or L or N),
			'X' to (H or K or L or N),
			'Y' to (H or K or M),
			'Z' to (A or D or K or L),
			'0' to (A or B or C or D or E or F),
			'1' to (B or C),
			'2' to (A or B or D or E or G1 or G2),
			'3' to (A or B or C or D or G2),
			'4' to (B or C or F or G1 or G2),
			'5' to (A or C or D or F or G1 or G2),
			'6' to (A or C or D or E or F or G1 or G2),
			'7' to (A or B or C),
			'8' to (A or B or C or D or E or F or G1 or G2),
			'9' to (A or B or C or D or F or G1 or G2),
			'-' to (G1 or G2),
			'=' to (D or G1 or G2),
			'+' to (J or M or G1 or G2),
			'*' to (H or J or K or L or M or N or G1 or G2),
			'/' to (K or L),
			'\\' to (H or N),
			'(' to (K or L),
			')' to (H or N),
			'[' to (A or D or E or F),
			']' to (A or B or C or D),
			'<' to (K or N),
			'>' to (H or L),
			'?' to (A or B or G2 or M),
			'_' to D,
			'|' to (J or M),
		)

	fun getMask(c: Char): Int = table[c.uppercaseChar()] ?: 0
}

fun textToVfd16BitMasks(
	text: String,
	maxDigits: Int = 22,
): List<Int> {
	val masks = mutableListOf<Int>()
	for (ch in text) {
		if (masks.size >= maxDigits && ch != '.' && ch != ',') break
		when (ch) {
			'.' -> {
				if (masks.isNotEmpty()) masks[masks.size - 1] = masks.last() or Vfd14Font.DP else masks.add(Vfd14Font.DP)
			}

			',' -> {
				if (masks.isNotEmpty()) {
					masks[masks.size - 1] = masks.last() or Vfd14Font.DP or Vfd14Font.COMMA
				} else {
					masks.add(Vfd14Font.DP or Vfd14Font.COMMA)
				}
			}

			else -> {
				if (masks.size < maxDigits) masks.add(Vfd14Font.getMask(ch))
			}
		}
	}
	while (masks.size < maxDigits) masks.add(0)
	return masks
}

// =====================================================================
// 5. HÖGNIVÅKOMPONENTER MED CALLOUTS OCH SILKSCREEN
// =====================================================================
data class LedCallout(
	val topLabel: String,
	val bottomLabel: String,
	val topColor: Color = Color.TextDim,
	val bottomColor: Color = Color.TextDim,
	val fontSize: Double = 2.0,
	val shelfWidth: Double = 3.5,
	val bottomStemH: Double = 2.4,
	val tierGap: Double = 3.0,
	val labelWidth: Double = 16.0,
)

class DualLedCallout(
	val leftLedCenter: Point,
	val rightLedCenter: Point,
	val ledTopY: Double,
	val callout: LedCallout,
	val lineColor: Color = Color.SilkscreenLine,
) : Component {
	val strokeW = 0.4
	val yBottom = ledTopY - callout.bottomStemH
	val yTop = yBottom - callout.tierGap
	val shelfX = rightLedCenter.x + callout.shelfWidth
	val halfH = callout.fontSize / 2.0

	val labelRectTop = Rect(shelfX + 1.0, yTop - halfH, callout.labelWidth, callout.fontSize)
	val labelRectBottom = Rect(shelfX + 1.0, yBottom - halfH, callout.labelWidth, callout.fontSize)

	override val structuralBounds: Rect = Rect(leftLedCenter.x, yTop, shelfX - leftLedCenter.x, ledTopY - yTop)
	override val visualBounds: Rect =
		run {
			val minX = leftLedCenter.x - strokeW / 2.0
			val maxX = labelRectTop.right
			val minY = yTop - halfH
			val maxY = ledTopY
			Rect(minX, minY, maxX - minX, maxY - minY)
		}

	override fun lower(): List<Primitive> =
		buildList {
			fun elbow(
				x1: Double,
				y1: Double,
				x2: Double,
				y2: Double,
			) = listOf(
				Primitive.PanelFrame(Rect(x1 - strokeW / 2.0, y2, strokeW, y1 - y2), fill = lineColor),
				Primitive.PanelFrame(Rect(x1, y2 - strokeW / 2.0, x2 - x1, strokeW), fill = lineColor),
			)
			addAll(elbow(leftLedCenter.x, ledTopY, shelfX, yTop))
			add(Primitive.Text(labelRectTop, callout.topLabel, callout.topColor, callout.fontSize, alignCenter = false))
			addAll(elbow(rightLedCenter.x, ledTopY, shelfX, yBottom))
			add(Primitive.Text(labelRectBottom, callout.bottomLabel, callout.bottomColor, callout.fontSize, alignCenter = false))
		}
}

class HardwareButton(
	val center: Point,
	val size: Size = Size(BASE_BUTTON_W, BASE_BUTTON_H),
	val template: ButtonTemplate,
	val topLabel: String? = null,
	val bottomLabel: String? = null,
	val binding: HardwareBinding? = null,
	val topLabelColor: Color = Color.TextDim,
	val bottomLabelColor: Color = Color.TextMain,
	val labelWidth: Double? = null,
) : Component {
	constructor(
		center: Point,
		archetype: ButtonArchetype,
		topLabel: String? = null,
		bottomLabel: String? = null,
		binding: HardwareBinding? = null,
		topLabelColor: Color = Color.TextDim,
		bottomLabelColor: Color = Color.TextMain,
		labelWidth: Double? = null,
	) : this(center, archetype.size, archetype.template, topLabel, bottomLabel, binding, topLabelColor, bottomLabelColor, labelWidth)

	override val structuralBounds = Rect.fromCenter(center, size)
	val effLabelW = labelWidth ?: structuralBounds.w

	override val visualBounds: Rect =
		run {
			val topY = if (topLabel != null) structuralBounds.y - 3.2 else structuralBounds.y
			val bottomY = if (bottomLabel != null) structuralBounds.bottom + 3.6 else structuralBounds.bottom
			val w = maxOf(structuralBounds.w, effLabelW)
			Rect(center.x - w / 2.0, topY, w, bottomY - topY)
		}

	override fun lower(): List<Primitive> =
		buildList {
			topLabel?.let { add(Primitive.Text(Rect.fromCenter(center.x, structuralBounds.y - 1.95, effLabelW, 2.5), it, topLabelColor, 2.0)) }
			add(Primitive.ButtonInstance(structuralBounds, template, binding))
			bottomLabel?.let {
				add(Primitive.Text(Rect.fromCenter(center.x, structuralBounds.bottom + 2.2, effLabelW, 2.8), it, bottomLabelColor, 2.2))
			}
		}
}

class DualLedPair(
	val buttonCenter: Point,
	val buttonSize: Size,
	val leftSignal: String,
	val leftTemplate: LedTemplate,
	val rightSignal: String,
	val rightTemplate: LedTemplate,
) : Component {
	val ledW = 2.2
	val ledH = 2.2
	val gap = 2.2
	val totalW = ledW * 2 + gap
	val startX get() = buttonCenter.x - (totalW / 2.0)
	val leftCenterX get() = startX + (ledW / 2.0)
	val rightCenterX get() = leftCenterX + ledW + gap
	val topY get() = buttonCenter.y - (buttonSize.h / 2.0) - 3.5

	val leftCenter get() = Point(leftCenterX, topY + ledH / 2.0)
	val rightCenter get() = Point(rightCenterX, topY + ledH / 2.0)

	override val structuralBounds = Rect(startX, topY, totalW, ledH)
	override val visualBounds: Rect get() = structuralBounds

	override fun lower(): List<Primitive> =
		buildList {
			add(Primitive.LedInstance(Rect(startX, topY, ledW, ledH), leftSignal, leftTemplate))
			add(Primitive.LedInstance(Rect(startX + ledW + gap, topY, ledW, ledH), rightSignal, rightTemplate))
		}
}

class LedButtonStrip(
	val center: Point,
	val label: String,
	val binding: HardwareBinding,
	val leftSignal: String,
	val leftTemplate: LedTemplate,
	val rightSignal: String,
	val rightTemplate: LedTemplate,
	val theme: SynthTheme,
	val callout: LedCallout? = null,
) : Component {
	val button =
		HardwareButton(
			center = center,
			archetype = theme.highButtonArchetype,
			bottomLabel = label,
			binding = binding,
			bottomLabelColor = theme.palette.textMain,
		)
	val leds =
		DualLedPair(
			buttonCenter = center,
			buttonSize = theme.highButtonArchetype.size,
			leftSignal = leftSignal,
			leftTemplate = leftTemplate,
			rightSignal = rightSignal,
			rightTemplate = rightTemplate,
		)
	val calloutComponent =
		callout?.let {
			DualLedCallout(
				leftLedCenter = leds.leftCenter,
				rightLedCenter = leds.rightCenter,
				ledTopY = leds.visualBounds.y,
				callout = it,
				lineColor = theme.palette.silkscreenLine,
			)
		}

	override val structuralBounds: Rect get() = button.structuralBounds
	override val visualBounds: Rect = listOfNotNull(button.visualBounds, leds.visualBounds, calloutComponent?.visualBounds).boundingRect()

	override fun lower(): List<Primitive> =
		buildList {
			addAll(leds.lower())
			addAll(button.lower())
			calloutComponent?.let { addAll(it.lower()) }
		}
}

class InputLevelMeters(
	val leftLedCenterX: Double,
	val rightLedCenterX: Double,
	val peakY: Double,
	val sigY: Double,
	val ledW: Double = 2.2,
	val ledH: Double = 2.2,
	val theme: SynthTheme,
) : Component {
	val cx = (leftLedCenterX + rightLedCenterX) / 2.0
	val gap = (rightLedCenterX - leftLedCenterX) - ledW
	val leftLedX = leftLedCenterX - ledW / 2.0
	val rightLedX = rightLedCenterX - ledW / 2.0

	override val structuralBounds: Rect =
		Rect(leftLedX, peakY - ledH / 2.0, (rightLedCenterX + ledW / 2.0) - leftLedX, (sigY - peakY) + ledH)

	override val visualBounds: Rect =
		run {
			val w = (rightLedCenterX + ledW / 2.0) - leftLedX + 4.0
			val totalH = (sigY - peakY) + 14.0
			Rect.fromCenter(cx, peakY + (totalH / 2.0) - 2.0, w, totalH)
		}

	override fun lower(): List<Primitive> =
		buildList {
			val p = theme.palette

			// 1. PEAK
			add(Primitive.LedInstance(Rect.fromCenter(leftLedCenterX, peakY, ledW, ledH), "asr10_peak_l", theme.redLed))
			add(Primitive.Text(Rect.fromCenter(cx, peakY, gap, 2.0), "PEAK", p.textDim, 1.8, alignCenter = true))
			add(Primitive.LedInstance(Rect.fromCenter(rightLedCenterX, peakY, ledW, ledH), "asr10_peak_r", theme.redLed))

			// 2. SIGNAL
			add(Primitive.LedInstance(Rect.fromCenter(leftLedCenterX, sigY, ledW, ledH), "asr10_sig_l", theme.greenLed))
			add(Primitive.Text(Rect.fromCenter(cx, sigY, gap, 2.0), "SIGNAL", p.textDim, 1.8, alignCenter = true))
			add(Primitive.LedInstance(Rect.fromCenter(rightLedCenterX, sigY, ledW, ledH), "asr10_sig_r", theme.greenLed))

			// 3. KANALBETECKNINGAR
			val labelY = sigY + (ledH / 2.0) + 2.4
			add(Primitive.Text(Rect.fromCenter(leftLedCenterX, labelY, 10.0, 1.6), "A - LEFT", p.textDim, 1.5, alignCenter = true))
			add(Primitive.Text(Rect.fromCenter(rightLedCenterX, labelY, 10.0, 1.6), "B - RIGHT", p.textDim, 1.5, alignCenter = true))

			// 4. HUVUDTITEL
			add(Primitive.Text(Rect.fromCenter(cx, labelY + 3.2, 22.0, 2.0), "INPUT LEVEL", p.textMain, 1.8, alignCenter = true))
		}
}

data class KeyDef(
	val topLabel: String,
	val bottomLabel: String,
	val tag: String,
	val mask: String,
)

class KeypadGrid(
	val center: Point,
	val rows: List<List<KeyDef>>,
	val pitchX: Double = BASE_BUTTON_W,
	val rowCentersY: List<Double>,
	val theme: SynthTheme,
) : Component {
	val buttons =
		run {
			val totalCols = rows.first().size
			val startX = center.x - ((totalCols - 1) * pitchX / 2.0)
			rows.flatMapIndexed { rIdx, row ->
				val cy = rowCentersY[rIdx]
				row.mapIndexed { cIdx, def ->
					HardwareButton(
						center = Point(startX + cIdx * pitchX, cy),
						archetype = theme.stdButtonArchetype,
						topLabel = def.topLabel,
						bottomLabel = def.bottomLabel,
						binding = HardwareBinding(def.tag, def.mask),
						topLabelColor = theme.palette.textDim,
						bottomLabelColor = theme.palette.textMain,
					)
				}
			}
		}

	override val structuralBounds: Rect = buttons.map { it.structuralBounds }.boundingRect()
	override val visualBounds: Rect = buttons.map { it.visualBounds }.boundingRect()

	override fun lower(): List<Primitive> = buttons.flatMap { it.lower() }
}

class Fader(
	val center: Point,
	val h: Double = 38.4,
	val knobW: Double = 11.2,
	val knobH: Double = 4.5,
	val label: String,
	val binding: HardwareBinding,
	val theme: SynthTheme,
) : Component {
	val slotW = 2.0
	val slotRect = Rect(center.x - slotW / 2.0, center.y - h / 2.0, slotW, h)
	val handleRect = Rect.fromCenter(center.x, center.y, knobW, knobH)

	override val structuralBounds: Rect = slotRect
	override val visualBounds: Rect = Rect(center.x - knobW / 2.0, slotRect.y, knobW, h + 12.0)

	override fun lower(): List<Primitive> =
		buildList {
			add(Primitive.PanelFrame(slotRect, fill = theme.palette.bg))
			add(Primitive.SliderHandle(handleRect, minY = slotRect.y, maxY = slotRect.bottom, tag = binding.tag, mask = binding.mask))
			add(Primitive.PanelFrame(Rect.fromCenter(center.x, center.y, knobW * 0.75, 0.5), fill = Color(0.9, 0.9, 0.9)))
			add(Primitive.Text(Rect.fromCenter(center.x, slotRect.bottom + 10.0, knobW + 12.0, 3.0), label, theme.palette.textMain, 2.2))
		}
}

// =====================================================================
// KOMPAKT OCH RELATIV VFD-DISPLAY
// =====================================================================
class VfdPrecisionDisplay(
	override val visualBounds: Rect,
	val previewText: String = "ASR-10 LOADING SYSTEM ",
	val theme: SynthTheme,
) : Component {
	override val structuralBounds: Rect get() = visualBounds

	override fun lower(): List<Primitive> =
		buildList {
			val p = theme.palette
			val root = Region(visualBounds.x, visualBounds.y, visualBounds.w, visualBounds.h, "VFD-Glas")

			// 1. Glasets bakgrund och yttre ram
			add(Primitive.PanelFrame(visualBounds, fill = p.vfdBg))
			val sw = 0.5
			add(Primitive.PanelFrame(Rect(root.x, root.y, root.w, sw), fill = p.silkscreenLine))
			add(Primitive.PanelFrame(Rect(root.x, root.bottom - sw, root.w, sw), fill = p.silkscreenLine))
			add(Primitive.PanelFrame(Rect(root.x, root.y, sw, root.h), fill = p.silkscreenLine))
			add(Primitive.PanelFrame(Rect(root.right - sw, root.y, sw, root.h), fill = p.silkscreenLine))

			// 2. Aktiv rityta
			val active = root.inset(root.w * 0.05, root.h * 0.08)
			val (annZone, filamentZone, digitZone) = active.splitRows(60.0, 25.0, 40.0)

			// 3. Glödtråd
			add(Primitive.FilamentLine(Rect(filamentZone.x, filamentZone.centerY, filamentZone.w, 0.2), p.filament))

			// 4. 22 st 14-segments tecken
			val masks = textToVfd16BitMasks(previewText, maxDigits = 22)
			val digitCols = digitZone.splitCols(*DoubleArray(22) { 1.0 })
			digitCols.forEachIndexed { i, col ->
				val cell = col.inset(col.w * 0.08, col.h * 0.05)
				add(
					Primitive.Vfd14Digit(
						bounds = cell.structuralBounds,
						digitIdx = i,
						underlineIdx = i + 22,
						rawMask = masks.getOrElse(i) { 0 },
						underlineLit = (i in 0..10),
					),
				)
			}

			// 5. Gemensam generator för annunciator-block
			fun renderAnnBlock(
				blockRegion: Region,
				wordMatrix: List<List<String>>,
			) {
				val rowRegions = blockRegion.splitRows(*DoubleArray(wordMatrix.size) { 1.0 })
				wordMatrix.forEachIndexed { rIdx, row ->
					val colRegions = rowRegions[rIdx].splitCols(*DoubleArray(row.size) { 1.0 })
					row.forEachIndexed { cIdx, token ->
						val cell = colRegions[cIdx].inset(colRegions[cIdx].w * 0.05, colRegions[cIdx].h * 0.10)
						val hasFrame = token.startsWith("*")
						val isAlwaysLit = token.endsWith("!")
						val parts = token.removePrefix("*").removeSuffix("!").split(":")
						val lit = PREVIEW_ALL_ANNUNCIATORS || isAlwaysLit
						add(
							Primitive.AnnunciatorWord(
								cell.structuralBounds,
								text = parts[0],
								hasFrame = hasFrame,
								bitIdx = parts.getOrNull(1)?.toIntOrNull(),
								isLit = lit,
							),
						)
					}
				}
			}

			val (leftAnnBlock, _, rightAnnBlock) = annZone.splitCols(47.0, 6.0, 47.0)

			renderAnnBlock(
				leftAnnBlock,
				listOf(
					listOf("*LOAD:15", "INST:14", "MIDI:12", "SYSTEM:12", "LAYER:11"),
					listOf("*CMD:13", "SEQ:2", "SONG:3", "PITCH:10", "FILTER:6"),
					listOf("*EDIT:5", "MACRO:4", "BANK:7", "LFO:9", "WAVE:8"),
				),
			)

			renderAnnBlock(
				rightAnnBlock,
				listOf(
					listOf("ENV", "*ODUB:19", "*REC:30", "PLAY:28", "STOP:27"),
					listOf("AMP:29", "SONG", "SEQ", "STEP", "REP"),
					listOf("TRACK!", "BAR!", "BEAT!", "CLOCK!"),
				),
			)
		}
}

// =====================================================================
// 6. LAYOUTCONTAINER & VALIDERING
// =====================================================================
data class BtnSpec(
	val center: Point,
	val top: String? = null,
	val bottom: String? = null,
	val mask: String? = null,
	val port: String = "buttons_0",
	val archetype: ButtonArchetype = theme.stdButtonArchetype,
	val labelWidth: Double? = null,
)

class LayoutContainer : Component {
	private val components = mutableListOf<Component>()
	val debugRegions = mutableListOf<Region>()

	fun add(component: Component) = components.add(component)

	fun addButton(
		center: Point,
		top: String? = null,
		bottom: String? = null,
		mask: String? = null,
		port: String = "buttons_0",
		archetype: ButtonArchetype = theme.stdButtonArchetype,
		labelWidth: Double? = null,
	): HardwareButton {
		val btn =
			HardwareButton(
				center = center,
				archetype = archetype,
				topLabel = top,
				bottomLabel = bottom,
				binding = mask?.let { HardwareBinding(port, it) },
				labelWidth = labelWidth,
			)
		add(btn)
		return btn
	}

	override val structuralBounds: Rect get() = components.map { it.structuralBounds }.boundingRect()
	override val visualBounds: Rect get() = components.combinedBounds

	override fun lower(): List<Primitive> = components.flatMap { it.lower() }
}

object LayoutValidator {
	fun validate(
		primitives: List<Primitive>,
		canvasBounds: Rect,
	) {
		var warningCount = 0

		// 1. Gränskontroll
		primitives.forEach { p ->
			if (p.bounds.x < canvasBounds.x - 0.05 || p.bounds.right > canvasBounds.right + 0.05 ||
				p.bounds.y < canvasBounds.y - 0.05 || p.bounds.bottom > canvasBounds.bottom + 0.05
			) {
				println("⚠️  LINT-VARNING: Element utanför canvas! Bounds=${p.bounds}")
				warningCount++
			}
		}

		// 2. Dubblettkontroll av knappbindningar
		val buttons = primitives.filterIsInstance<Primitive.ButtonInstance>()
		val bindings = mutableMapOf<String, Primitive.ButtonInstance>()
		buttons.forEach { btn ->
			btn.binding?.let { b ->
				val cleanTag = b.tag.removePrefix(":").removePrefix("panel:")
				if (cleanTag != "buttons_0" && cleanTag != "buttons_32") {
					println("⚠️  LINT-VARNING: Oväntad knapp-port '${b.tag}'! Förväntade 'buttons_0' eller 'buttons_32'")
					warningCount++
				}
				val key = "$cleanTag:${b.mask}"
				if (bindings.containsKey(key)) {
					println("⚠️️  LINT-VARNING: Dubblettknapp-bindning detekterad! Tag=${b.tag}, Mask=${b.mask}")
					warningCount++
				} else {
					bindings[key] = btn
				}
			}
		}

		// 3. Fysiska kollisioner mellan interaktiva element
		val interactiveElements =
			primitives.filter {
				it is Primitive.ButtonInstance || it is Primitive.SliderHandle || it is Primitive.Vfd14Digit || it is Primitive.AnnunciatorWord
			}

		fun elementName(p: Primitive): String =
			when (p) {
				is Primitive.ButtonInstance -> "Knapp (${p.template.id})"
				is Primitive.SliderHandle -> "Fader (${p.tag})"
				is Primitive.Vfd14Digit -> "VFD-siffra (${p.digitIdx})"
				is Primitive.AnnunciatorWord -> "Annunciator (${p.text})"
				else -> "Element"
			}

		for (i in 0 until interactiveElements.size) {
			for (j in i + 1 until interactiveElements.size) {
				val e1 = interactiveElements[i]
				val e2 = interactiveElements[j]
				if (e1.bounds.intersects(e2.bounds)) {
					println("⚠️  LINT-VARNING: Kollision detekterad mellan ${elementName(e1)} vid ${e1.bounds} och ${elementName(e2)} vid ${e2.bounds}")
					warningCount++
				}
			}
		}

		// 4. Kollisionsdetektering mellan text och knappar
		val textElements = primitives.filterIsInstance<Primitive.Text>()
		for (i in 0 until textElements.size) {
			for (j in i + 1 until textElements.size) {
				val t1 = textElements[i]
				val t2 = textElements[j]
				if (t1.bounds.intersects(t2.bounds, tolerance = 0.05)) {
					println("⚠️  LINT-VARNING: Textöverlapp detekterad mellan \"${t1.content}\" vid ${t1.bounds} och \"${t2.content}\" vid ${t2.bounds}")
					warningCount++
				}
			}
		}

		for (t in textElements) {
			for (b in buttons) {
				if (t.bounds.intersects(b.bounds, tolerance = 0.05)) {
					println("⚠️  LINT-VARNING: Text \"${t.content}\" vid ${t.bounds} överlappar knapp vid ${b.bounds}")
					warningCount++
				}
			}
		}

		if (warningCount == 0) {
			println("✅ LayoutValidering godkänd: 0 varningar (gränskontroll, bindningar, textplacering och hitboxes verifierade).")
		} else {
			System.err.println("❌ LayoutValidering misslyckades med $warningCount varning(ar)!")
			System.exit(1)
		}
	}
}

// =====================================================================
// 7. MÅLRENDERARE: MAME XML & SVG HTML
// =====================================================================
fun labelMameInfo(content: String): Pair<String, String> =
	when (content) {
		"▲" -> {
			"arr_up" to "&#x25B2;"
		}

		"▼" -> {
			"arr_down" to "&#x25BC;"
		}

		"◄" -> {
			"arr_left" to "&#x25C4;"
		}

		"►" -> {
			"arr_right" to "&#x25BA;"
		}

		else -> {
			val filtered = content.filter { it.isLetterOrDigit() }.lowercase()
			val safeId = if (filtered.isEmpty()) "sym" else filtered
			safeId to content
		}
	}

class MameRenderer(
	val width: Double,
	val height: Double,
	val theme: SynthTheme,
) {
	fun labelElementName(p: Primitive.Text): String {
		val (baseId, _) = labelMameInfo(p.content)
		val colorHex =
			p.color.hex
				.removePrefix("#")
				.lowercase()
		return "lbl_${baseId}_$colorHex"
	}

	fun annElementName(word: Primitive.AnnunciatorWord): String {
		val prefix = if (word.hasFrame) "ann_box" else "ann"
		return "${prefix}_${word.text.lowercase()}"
	}

	fun resolveInputTag(rawTag: String): String = ":panel:${rawTag.removePrefix(":").removePrefix("panel:")}"

	fun render(primitives: List<Primitive>): String {
		val usedButtonTemplates = primitives.filterIsInstance<Primitive.ButtonInstance>().map { it.template }.distinctBy { it.id }
		val usedLedTemplates = primitives.filterIsInstance<Primitive.LedInstance>().map { it.template }.distinctBy { it.id }
		val uniqueLabels = primitives.filterIsInstance<Primitive.Text>().distinctBy { labelElementName(it) }
		val uniqueAnnWords = primitives.filterIsInstance<Primitive.AnnunciatorWord>().distinctBy { annElementName(it) }
		val hasFaders = primitives.any { it is Primitive.SliderHandle }
		val hasFilament = primitives.any { it is Primitive.FilamentLine }
		val p = theme.palette

		return xml("mamelayout") {
			attr("version", "2")

			tag("element") {
				attr("name", "bg")
				attr("defstate", "0")
				rect(c = p.bg)
			}

			primitives.filterIsInstance<Primitive.PanelFrame>().distinctBy { it.fill.hex.lowercase() }.forEach { pf ->
				tag("element") {
					attr("name", "panelfill_${pf.fill.hex.removePrefix("#").lowercase()}")
					attr("defstate", "0")
					rect(c = pf.fill)
				}
			}

			tag("element") {
				attr("name", "vfd0")
				attr("defstate", "0")
				tag("led14segsc") { color(p.vfdGlow) }
			}
			tag("element") {
				attr("name", "underline")
				attr("defstate", "0")
				rect(state = "0", c = p.vfdDim)
				rect(state = "1", c = p.vfdGlow)
			}

			if (hasFilament) {
				tag("element") {
					attr("name", "filament")
					attr("defstate", "0")
					rect(c = p.filament)
				}
			}

			uniqueAnnWords.forEach { word ->
				tag("element") {
					attr("name", annElementName(word))
					attr("defstate", "1")
					if (word.hasFrame) {
						rect(state = "0", c = p.vfdGlow)
						tag("text") {
							attr("state", "0")
							attr("string", word.text)
							bounds(0.08, 0.12, 0.84, 0.76)
							color(Color(0.00, 0.00, 0.00))
						}
						rect(state = "1", c = p.vfdDim)
						tag("text") {
							attr("state", "1")
							attr("string", word.text)
							bounds(0.08, 0.12, 0.84, 0.76)
							color(Color(0.00, 0.00, 0.00))
						}
					} else {
						tag("text") {
							attr("state", "0")
							attr("string", word.text)
							color(p.vfdGlow)
						}
						tag("text") {
							attr("state", "1")
							attr("string", word.text)
							color(p.vfdDim)
						}
					}
				}
			}

			usedButtonTemplates.forEach { t ->
				tag("element") {
					attr("name", t.id)
					attr("defstate", "0")
					rect(c = t.outerRim)
					rect(x = 0.04, y = 0.06, w = 0.92, h = 0.88, c = t.bodyBevel)
					rect(state = "0", x = 0.08, y = 0.12, w = 0.84, h = 0.76, c = t.bodyReleased)
					rect(state = "1", x = 0.08, y = 0.12, w = 0.84, h = 0.76, c = t.bodyPressed)
				}
			}

			usedLedTemplates.forEach { t ->
				tag("element") {
					attr("name", t.id)
					attr("defstate", "0")
					rect(state = "0", c = t.offColor)
					rect(state = "1", c = t.onColor)
				}
			}

			if (hasFaders) {
				tag("element") {
					attr("name", "fader_slot")
					attr("defstate", "0")
					rect(c = Color(0.02, 0.02, 0.03))
				}
				tag("element") {
					attr("name", "fader_knob")
					attr("defstate", "0")
					rect(c = Color(0.26, 0.27, 0.30))
				}
				tag("element") {
					attr("name", "fader_line")
					attr("defstate", "0")
					rect(c = Color(0.90, 0.90, 0.90))
				}
			}

			uniqueLabels.forEach { txt ->
				tag("element") {
					attr("name", labelElementName(txt))
					attr("defstate", "0")
					tag("text") {
						if (!txt.alignCenter) attr("align", "1")
						attr("string", labelMameInfo(txt.content).second)
						color(txt.color)
					}
				}
			}

			tag("view") {
				attr("name", "ASR-10 Precision Console")

				tag("element") {
					attr("ref", "bg")
					bounds(0, 0, width, height)
				}
				primitives.filterIsInstance<Primitive.PanelFrame>().forEach {
					item(
						"panelfill_${it.fill.hex.removePrefix("#").lowercase()}",
						it.bounds,
					)
				}
				primitives.filterIsInstance<Primitive.FilamentLine>().forEach { item("filament", it.bounds) }
				primitives.filterIsInstance<Primitive.Text>().forEach { item(labelElementName(it), it.bounds) }

				primitives.filterIsInstance<Primitive.Vfd14Digit>().forEach { pDigit ->
					viewItem("vfd0", pDigit.bounds, name = "vfd${pDigit.digitIdx}")
					viewItem("underline", Rect(pDigit.bounds.x, pDigit.bounds.bottom + 0.4, pDigit.bounds.w, 0.8), name = "vfd${pDigit.underlineIdx}")
				}

				primitives.filterIsInstance<Primitive.AnnunciatorWord>().forEach { pWord ->
					viewItem(annElementName(pWord), pWord.bounds, name = pWord.bitIdx?.let { "asr10_annbit$it" })
				}

				primitives.filterIsInstance<Primitive.LedInstance>().forEach { pLed ->
					viewItem(pLed.template.id, pLed.bounds, name = pLed.signalName)
				}

				primitives.filterIsInstance<Primitive.SliderHandle>().forEach { pFader ->
					val slotH = pFader.maxY - pFader.minY + pFader.bounds.h
					item("fader_slot", Rect(pFader.bounds.cx - 1.0, pFader.minY, 2.0, slotH))
					viewItem("fader_knob", pFader.bounds, inputTag = resolveInputTag(pFader.tag), inputMask = pFader.mask)
					viewItem("fader_line", Rect(pFader.bounds.x + 2.0, pFader.bounds.cy - 0.3, pFader.bounds.w - 4.0, 0.6), clickthrough = "yes")
				}

				primitives.filterIsInstance<Primitive.ButtonInstance>().forEach { pBtn ->
					viewItem(
						ref = pBtn.template.id,
						b = pBtn.bounds,
						inputTag = pBtn.binding?.let { resolveInputTag(it.tag) },
						inputMask = pBtn.binding?.mask,
					)
				}
			}
		}
	}
}

class SvgHtmlRenderer(
	val width: Double,
	val height: Double,
	val debug: Boolean = false,
) {
	private fun render14SegmentSvg(p: Primitive.Vfd14Digit): String {
		val mask = p.rawMask

		fun seg(
			bit: Int,
			points: String,
		) = "<polygon class=\"${if ((mask and bit) != 0) "vfd-seg lit" else "vfd-seg off"}\" points=\"$points\" />"

		val segPolygons =
			listOf(
				Vfd14Font.A to "16,6 84,6 74,17 26,17",
				Vfd14Font.B to "84,20 93,12 93,76 83,70",
				Vfd14Font.C to "83,88 93,82 93,146 84,138",
				Vfd14Font.D to "26,141 74,141 84,152 16,152",
				Vfd14Font.E to "7,82 17,88 17,138 7,146",
				Vfd14Font.F to "7,12 17,20 17,70 7,76",
				Vfd14Font.G1 to "20,79 26,73 45,73 45,85 26,85",
				Vfd14Font.G2 to "55,73 74,73 80,79 74,85 55,85",
				Vfd14Font.J to "46,20 54,20 54,71 46,71",
				Vfd14Font.M to "46,87 54,87 54,138 46,138",
				Vfd14Font.H to "22,24 31,21 44,67 36,71",
				Vfd14Font.K to "69,21 78,24 64,71 56,67",
				Vfd14Font.L to "36,87 44,91 31,137 22,134",
				Vfd14Font.N to "56,91 64,87 78,134 69,137",
			).joinToString(" ") { (bit, pts) -> seg(bit, pts) }

		val dpLit = (mask and Vfd14Font.DP) != 0
		val commaLit = (mask and Vfd14Font.COMMA) != 0
		val dp = "<circle class=\"${if (dpLit) "vfd-seg lit" else "vfd-seg off"}\" cx=\"95\" cy=\"148\" r=\"4.0\" />"
		val comma = "<polygon class=\"${if (commaLit) "vfd-seg lit" else "vfd-seg off"}\" points=\"93,150 97,150 94,158 92,156\" />"

		return """
			<svg x="${p.bounds.x}" y="${p.bounds.y}" width="${p.bounds.w}" height="${p.bounds.h}" viewBox="0 0 100 160">
			    <g>
			        $segPolygons $dp $comma
			    </g>
			</svg>
			<rect class="${if (p.underlineLit) "vfd-und lit" else "vfd-und off"}" x="${p.bounds.x}" y="${p.bounds.bottom + 0.4}" width="${p.bounds.w}" height="0.8" />
			""".trimIndent()
	}

	fun render(
		primitives: List<Primitive>,
		debugRegions: List<Region> = emptyList(),
	): String {
		val svgElements =
			primitives.joinToString("\n\t\t\t\t") { p ->
				when (p) {
					is Primitive.Vfd14Digit -> {
						render14SegmentSvg(p)
					}

					is Primitive.AnnunciatorWord -> {
						val litClass = if (p.isLit) "lit" else "off"
						if (p.hasFrame) {
							"""
							<g class="ann-badge $litClass">
							    <rect class="badge-bg" x="${p.bounds.x}" y="${p.bounds.y}" width="${p.bounds.w}" height="${p.bounds.h}" />
							    <text class="badge-text" x="${p.bounds.cx}" y="${p.bounds.cy + 0.85}" font-size="2.3" text-anchor="middle">${p.text}</text>
							</g>
							""".trimIndent()
						} else {
							"""
							<g class="ann-word $litClass">
							    <text x="${p.bounds.cx}" y="${p.bounds.cy + 0.85}" font-size="2.6" text-anchor="middle">${p.text}</text>
							</g>
							""".trimIndent()
						}
					}

					is Primitive.FilamentLine -> {
						"<line class=\"vfd-filament\" x1=\"${p.bounds.x}\" y1=\"${p.bounds.cy}\" x2=\"${p.bounds.right}\" y2=\"${p.bounds.cy}\" stroke=\"${p.color.hex}\" stroke-width=\"${p.bounds.h}\" opacity=\"0.6\" />"
					}

					is Primitive.PanelFrame -> {
						"<rect x=\"${p.bounds.x}\" y=\"${p.bounds.y}\" width=\"${p.bounds.w}\" height=\"${p.bounds.h}\" fill=\"${p.fill.hex}\" />"
					}

					is Primitive.Text -> {
						val anchor = if (p.alignCenter) "middle" else "start"
						val tx = if (p.alignCenter) p.bounds.cx else p.bounds.x
						"<text x=\"$tx\" y=\"${p.bounds.cy + p.size * 0.35}\" fill=\"${p.color.hex}\" font-size=\"${p.size}\" text-anchor=\"$anchor\">${p.content}</text>"
					}

					is Primitive.ButtonInstance -> {
						val capX = p.bounds.x + p.bounds.w * 0.08
						val capY = p.bounds.y + p.bounds.h * 0.12
						val capW = p.bounds.w * 0.84
						val capH = p.bounds.h * 0.76
						"""<rect class="btn-rim" x="${p.bounds.x}" y="${p.bounds.y}" width="${p.bounds.w}" height="${p.bounds.h}" fill="${p.template.outerRim.hex}" />
                    <rect class="btn-body" x="${p.bounds.x + p.bounds.w * 0.04}" y="${p.bounds.y + p.bounds.h * 0.06}" width="${p.bounds.w * 0.92}" height="${p.bounds.h * 0.88}" fill="${p.template.bodyBevel.hex}" />
                    <rect class="btn-cap" x="$capX" y="$capY" width="$capW" height="$capH" fill="${p.template.bodyReleased.hex}" />
                    <rect class="hitbox" x="${p.bounds.x}" y="${p.bounds.y}" width="${p.bounds.w}" height="${p.bounds.h}" />"""
					}

					is Primitive.LedInstance -> {
						"<rect class=\"led\" x=\"${p.bounds.x}\" y=\"${p.bounds.y}\" width=\"${p.bounds.w}\" height=\"${p.bounds.h}\" fill=\"${p.template.cssHex}\" />"
					}

					is Primitive.SliderHandle -> {
						val slotH = p.maxY - p.minY + p.bounds.h
						"""<rect class="fader-slot" x="${p.bounds.cx - 1.0}" y="${p.minY}" width="2.0" height="$slotH" />
                    <g class="fader-group" data-min-y="${p.minY}" data-max-y="${p.maxY}">
                       <rect class="fader-knob" x="${p.bounds.x}" y="${p.bounds.y}" width="${p.bounds.w}" height="${p.bounds.h}" />
                       <line class="fader-line" x1="${p.bounds.x + 2.0}" y1="${p.bounds.cy}" x2="${p.bounds.x + p.bounds.w - 2.0}" y2="${p.bounds.cy}" />
                    </g>"""
					}
				}
			}

		val debugSvg =
			if (debug) {
				debugRegions.joinToString("\n\t\t\t\t") { r ->
					"""<rect x="${r.x}" y="${r.y}" width="${r.w}" height="${r.h}" fill="none" stroke="rgba(0, 255, 255, 0.4)" stroke-width="0.3" stroke-dasharray="1.5,1.5" />
                   <text x="${r.x + 1.0}" y="${r.y + 3.0}" fill="cyan" font-size="1.8" opacity="0.7">${r.label}</text>"""
				}
			} else {
				""
			}

		return """
			<!DOCTYPE html>
			<html>
			<head>
			    <meta charset="utf-8">
			    <title>ASR-10 Precision Console Preview - Relational Layout DSL & Authentic Matrix</title>
			    <style>
			        body { background: #08080a; display: flex; flex-direction: column; justify-content: center; align-items: center; min-height: 100vh; margin: 0; user-select: none; }
			        svg { width: 95vw; max-width: 1400px; background: #0f1013; border: 1px solid #222; box-shadow: 0 10px 30px rgba(0,0,0,0.8); }
			        text { font-family: Arial, Helvetica, sans-serif; pointer-events: none; font-weight: normal; }

			        .hitbox { fill: transparent; stroke: rgba(255,255,255,0.05); stroke-width: 0.2; cursor: pointer; transition: 0.05s; }
			        .hitbox:hover { fill: rgba(255,255,255,0.06); }
			        .hitbox:active { fill: rgba(89, 140, 178, 0.5); }

			        .btn-rim { stroke: rgba(0,0,0,0.6); stroke-width: 0.3; }
			        .btn-body { stroke: rgba(255,255,255,0.08); stroke-width: 0.2; }
			        .btn-cap { stroke: rgba(0,0,0,0.3); stroke-width: 0.2; }

			        .led { opacity: 0.9; stroke: rgba(0,0,0,0.5); stroke-width: 0.2; }

			        .fader-slot { fill: #020203; stroke: #121215; stroke-width: 0.3; }
			        .fader-group { cursor: grab; }
			        .fader-group:active { cursor: grabbing; }
			        .fader-knob { fill: #2b2e34; stroke: #525866; stroke-width: 0.4; }
			        .fader-line { stroke: #eee; stroke-width: 0.6; }

			        .vfd-filament { stroke: #264040; }
			        .vfd-seg.off { fill: #0a1819; stroke: #050e0f; stroke-width: 1.0; opacity: 0.10; }
			        .vfd-seg.lit { fill: #73fff2; }
			        .vfd-und.off { fill: #0a1819; opacity: 0.10; }
			        .vfd-und.lit { fill: #73fff2; }

			        .ann-word.off text { fill: #081717; opacity: 0.20; }
			        .ann-word.lit text { fill: #73fff2; }

			        .ann-badge.off .badge-bg { fill: #081717; opacity: 0.20; }
			        .ann-badge.off .badge-text { fill: #020505; }
			        .ann-badge.lit .badge-bg { fill: #73fff2; }
			        .ann-badge.lit .badge-text { fill: #020505; }
			    </style>
			</head>
			<body>
			    <svg viewBox="0 0 $width $height">
			        $svgElements
			        $debugSvg
			    </svg>
			</body>
			</html>
			""".trimIndent()
	}
}

// =====================================================================
// 8. BYGG PANELEN RELATIONELLT
// =====================================================================
val panel =
	LayoutContainer().apply {
		val DEBUG_REGIONS = false

		// 1. Makrozoner i höjdled
		val base = Region(0.0, 0.0, PANEL_WIDTH, PANEL_HEIGHT, "Root")
		val (headerZone, displayZone, strip1Zone, mainZone) = base.splitRows(11.0, 38.0, 31.0, 58.0)

		if (DEBUG_REGIONS) debugRegions.addAll(listOf(headerZone, displayZone, strip1Zone, mainZone))

		// Gemensamma lodräta kanalaxlar A och B (12 mm delning)
		val audioTrackX_A = 186.0
		val audioTrackX_B = audioTrackX_A + BASE_BUTTON_W

		// --- 1. HEADER ---
		add(Primitive.PanelFrame(headerZone.inset(4.0, 2.5).structuralBounds, fill = theme.palette.panelFrame))
		add(
			Primitive.Text(
				Rect(headerZone.left + 4.0, headerZone.centerY - 1.5, 24.0, 3.0),
				"ENSONIQ",
				theme.palette.textMain,
				3.2,
				alignCenter = false,
			),
		)
		add(
			Primitive.Text(
				Rect(headerZone.left + 32.0, headerZone.centerY - 1.5, 138.0, 3.0),
				"ASR-10 Advanced Sampling Recorder",
				theme.palette.textMain,
				3.0,
				alignCenter = false,
			),
		)

		// --- 2. DISPLAY ZONE ---
		val (dispCol, _) = displayZone.inset(4.0, 1.0).splitCols(79.0, 21.0)
		val vfdBounds = Rect.fromCenter(Point(dispCol.centerX, dispCol.centerY), Size(166.0, 35.0))
		add(VfdPrecisionDisplay(vfdBounds, previewText = "ASR-10 LOADING SYSTEM ", theme = theme))

		add(
			InputLevelMeters(
				leftLedCenterX = audioTrackX_A,
				rightLedCenterX = audioTrackX_B,
				peakY = displayZone.centerY - 3.5,
				sigY = displayZone.centerY + 3.0,
				theme = theme,
			),
		)

		// --- 3. STRIP 1 (Spår 1-8, Transport & Audio Tracks) ---
		val (stripLeftCol, _) = strip1Zone.inset(4.0, 0.0).splitCols(79.0, 21.0)

		// A. Spår 1-8 via datatabell (dikt an, 0 mm glipa)
		val trackBankW = 8 * BASE_BUTTON_W
		val trackZone = Region(stripLeftCol.left + 5.0, strip1Zone.y, trackBankW, strip1Zone.h, "Tracks")
		val trackXPositions = trackZone.hSpan.distribute(8, BASE_BUTTON_W)

		val trackBindings =
			listOf(
				"buttons_0" to "0x00000004",
				"buttons_0" to "0x00000100",
				"buttons_0" to "0x00004000",
				"buttons_0" to "0x00100000",
				"buttons_0" to "0x00000010",
				"buttons_32" to "0x00000004",
				"buttons_0" to "0x10000000",
				"buttons_0" to "0x00400000",
			)

		val trackStrips =
			trackXPositions.mapIndexed { idx, cx ->
				val (port, mask) = trackBindings[idx]
				val callout = if (idx == 7) LedCallout(topLabel = "Loaded", bottomLabel = "Selected") else null
				LedButtonStrip(
					center = Point(cx, strip1Zone.centerY),
					label = "${idx + 1}",
					binding = HardwareBinding(port, mask),
					leftSignal = "asr10_loadedlamp$idx",
					leftTemplate = theme.yellowLed,
					rightSignal = "asr10_instlamp$idx",
					rightTemplate = theme.redLed,
					theme = theme,
					callout = callout,
				)
			}
		trackStrips.forEach { add(it) }

		val trackRowBounds = trackStrips.map { it.structuralBounds }.boundingRect()
		add(Primitive.Text(Rect.fromCenter(trackRowBounds.cx, trackRowBounds.bottom + 5.5, 36.0, 2.0), "Instruments", theme.palette.textDim, 1.8))
		add(
			Primitive.Text(
				Rect.fromCenter(trackRowBounds.cx, trackRowBounds.bottom + 8.0, 42.0, 2.0),
				"Sequence Tracks",
				theme.palette.textDim,
				1.8,
			),
		)

		// B. Transport (Record, Stop, Play dikt an)
		val transportBankW = 3 * BASE_BUTTON_W
		val transportZone = Region(stripLeftCol.right - transportBankW - 4.0, strip1Zone.y, transportBankW, strip1Zone.h, "Transport")
		val transportCentersX = transportZone.hSpan.distribute(3, BASE_BUTTON_W)

		listOf(
			Triple("Record", null, "0x00000008"),
			Triple("Stop", "Continue", "0x00800000"),
			Triple("Play", null, "0x20000000"),
		).forEachIndexed { i, (top, bot, mask) ->
			addButton(Point(transportCentersX[i], strip1Zone.centerY), top = top, bottom = bot, mask = mask, archetype = theme.highButtonArchetype)
		}

		// C. Audio Tracks A & B
		val audioA =
			LedButtonStrip(
				center = Point(audioTrackX_A, strip1Zone.centerY),
				label = "A",
				binding = HardwareBinding("buttons_0", "0x00000001"),
				leftSignal = "asr10_audio_a_sig",
				leftTemplate = theme.greenLed,
				rightSignal = "asr10_audio_a_sel",
				rightTemplate = theme.yellowLed,
				theme = theme,
			)
		val audioB =
			LedButtonStrip(
				center = Point(audioTrackX_B, strip1Zone.centerY),
				label = "B",
				binding = HardwareBinding("buttons_0", "0x00000002"),
				leftSignal = "asr10_audio_b_sig",
				leftTemplate = theme.greenLed,
				rightSignal = "asr10_audio_b_sel",
				rightTemplate = theme.yellowLed,
				theme = theme,
				callout = LedCallout(topLabel = "Source Monitor", bottomLabel = "Selected", labelWidth = 14.0),
			)
		add(audioA)
		add(audioB)

		val audioMid = audioA.midpointTo(audioB)
		add(
			Primitive.Text(Rect.fromCenter(audioMid.x, audioA.structuralBounds.bottom + 5.5, 30.0, 2.0), "Audio Tracks", theme.palette.textDim, 1.8),
		)

		// --- 4. MAIN MATRIX (Mode, Page, Keypad, Nav & Faders) ---
		val mainActiveSpan = Span(start = mainZone.top + 7.0, end = mainZone.top + 53.0)
		val pageCentersY = mainActiveSpan.distribute(4, theme.stdButtonArchetype.height)

		val cmdCenterY = (pageCentersY[1] + pageCentersY[2]) / 2.0
		val modeGap = 8.0
		val modeStep = theme.highButtonArchetype.height + modeGap
		val modeCentersY = listOf(cmdCenterY - modeStep, cmdCenterY, cmdCenterY + modeStep)

		val modeCx = trackStrips[0].centerX // Rakt under Instrument 1
		val pageCx = trackStrips[2].centerX // Rakt under Instrument 3
		val keypadCx = trackStrips[5].centerX // Rakt under Instrument 6 (centrum för 5, 6, 7)

		// Mode (3 knappar)
		listOf(
			Triple("Load", "(Select Preset)", "0x04000000"),
			Triple("Command", "(Create Preset)", "0x00000040"),
			Triple("Edit", null, "0x00000020"),
		).forEachIndexed { i, (top, bot, mask) ->
			addButton(
				Point(modeCx, modeCentersY[i]),
				top = top,
				bottom = bot,
				mask = mask,
				archetype = theme.highButtonArchetype,
				labelWidth = if (bot != null) 16.0 else null,
			)
		}

		// Page (4 knappar)
		listOf(
			Triple("Instrument", null, "0x00008000"),
			Triple("Seq Song", null, "0x00200000"),
			Triple("System MIDI", "(Directory)", "0x08000000"),
			Triple("Effects", null, "0x00000200"),
		).forEachIndexed { i, (top, bot, mask) ->
			addButton(Point(pageCx, pageCentersY[i]), top = top, bottom = bot, mask = mask, labelWidth = if (bot != null) 14.0 else null)
		}

		// Keypad (3x4 dikt an under spår 5, 6, 7)
		val keypadRows =
			listOf(
				listOf(
					KeyDef("Env 1", "1", "buttons_0", "0x00002000"),
					KeyDef("Env 2", "2", "buttons_0", "0x00040000"),
					KeyDef("Env 3", "3", "buttons_0", "0x00080000"),
				),
				listOf(
					KeyDef("Pitch", "4", "buttons_0", "0x01000000"),
					KeyDef("Filter", "5", "buttons_0", "0x02000000"),
					KeyDef("Amp", "6", "buttons_0", "0x40000000"),
				),
				listOf(
					KeyDef("LFO", "7", "buttons_0", "0x80000000"),
					KeyDef("Wave", "8", "buttons_32", "0x00000010"),
					KeyDef("Layer", "9", "buttons_32", "0x00000020"),
				),
				listOf(
					KeyDef("Sample", "Source Select", "buttons_32", "0x00000001"),
					KeyDef("Track", "0", "buttons_0", "0x00001000"),
					KeyDef("FX Select", "FX Bypass", "buttons_0", "0x00000080"),
				),
			)
		add(
			KeypadGrid(
				center = Point(keypadCx, (mainActiveSpan.start + mainActiveSpan.end) / 2.0),
				rows = keypadRows,
				pitchX = BASE_BUTTON_W,
				rowCentersY = pageCentersY,
				theme = theme,
			),
		)

		// Navigering (Låst direkt under Transporten)
		val navLeftCx = transportCentersX[0]
		val navCx = transportCentersX[1]
		val navRightCx = transportCentersX[2]
		val editCenterY = modeCentersY[2]

		listOf(
			BtnSpec(Point(navCx, pageCentersY[0]), top = "▲", mask = "0x00000400"),
			BtnSpec(Point(navCx, pageCentersY[1]), bottom = "▼", mask = "0x00000800"),
			BtnSpec(Point(navLeftCx, pageCentersY[1]), top = "◄", mask = "0x00010000"),
			BtnSpec(Point(navRightCx, pageCentersY[1]), top = "►", mask = "0x00020000"),
			BtnSpec(
				Point(navLeftCx, editCenterY),
				top = "Cancel",
				bottom = "No",
				mask = "0x00000002",
				port = "buttons_32",
				archetype = theme.highButtonArchetype,
			),
			BtnSpec(
				Point(navRightCx, editCenterY),
				top = "Enter",
				bottom = "Yes",
				mask = "0x00000008",
				port = "buttons_32",
				archetype = theme.highButtonArchetype,
			),
		).forEach { addButton(it.center, it.top, it.bottom, it.mask, it.port, it.archetype, it.labelWidth) }

		// --- 5. FADERS ---
		val faderCenterY = 104.0
		val keypadRightColX = keypadCx + BASE_BUTTON_W
		val dataEntryX = (keypadRightColX + navLeftCx) / 2.0

		add(
			Fader(
				center = Point(dataEntryX, faderCenterY),
				label = "Data Entry",
				binding = HardwareBinding("analog_data_entry", "0x3ff"),
				theme = theme,
			),
		)
		add(Fader(center = Point(audioMid.x, faderCenterY), label = "Volume", binding = HardwareBinding("analog_volume", "0x3ff"), theme = theme))
	}

// =====================================================================
// EXEKVERING & FILGENERERING
// =====================================================================
fun findProjectRoot(): File {
	var dir: File? = File(System.getProperty("user.dir")).canonicalFile
	while (dir != null) {
		if (File(dir, "src/mame/ensoniq/asr10_boot.cpp").exists() && File(dir, "makefile").exists()) {
			return dir
		}
		dir = dir.parentFile
	}
	dir = File(".").canonicalFile
	while (dir != null) {
		if (File(dir, "src/mame/ensoniq/asr10_boot.cpp").exists() && File(dir, "makefile").exists()) {
			return dir
		}
		dir = dir.parentFile
	}
	System.err.println("❌ FEL: Kunde inte lokalisera MAME-projektets rotkatalog!")
	System.err.println("   Letade efter 'src/mame/ensoniq/asr10_boot.cpp' och 'makefile' uppåt från: ${System.getProperty("user.dir")}")
	System.exit(1)
	throw IllegalStateException("Project root not found")
}

val projectRoot = findProjectRoot()
println("📁 Projektrot identifierad: ${projectRoot.canonicalPath}")

val primitives = panel.lower()
val canvasBounds = Rect(0.0, 0.0, PANEL_WIDTH, PANEL_HEIGHT)

// 1. Validera layouten med lintern
LayoutValidator.validate(primitives, canvasBounds)

// 2. Rendera MAME XML och SVG HTML
val mameXml = MameRenderer(PANEL_WIDTH, PANEL_HEIGHT, theme).render(primitives)
val svgHtml = SvgHtmlRenderer(PANEL_WIDTH, PANEL_HEIGHT, debug = false).render(primitives, panel.debugRegions)

// 3. Robust filskrivning till exakta målsökvägar i projektroten
fun saveOutputFile(
	file: File,
	content: String,
	label: String,
) {
	try {
		val parent = file.parentFile
		if (parent != null && !parent.exists()) {
			if (!parent.mkdirs()) {
				System.err.println("❌ FEL: Kunde inte skapa katalog: ${parent.canonicalPath}")
				System.exit(1)
			}
		}
		file.writeText(content)
		val lineCount = content.lines().size
		val byteCount = file.length()
		println("💾 Sparade $label ($lineCount rader, $byteCount bytes) till: ${file.canonicalPath}")
	} catch (e: Exception) {
		System.err.println("❌ FEL vid skrivning av $label till ${file.canonicalPath}: ${e.message}")
		e.printStackTrace()
		System.exit(1)
	}
}

val internalLayFile = File(projectRoot, "src/mame/layout/asr10_panel.lay")
val artworkLayFile = File(projectRoot, "artwork/asr10booth/default.lay")
val previewHtmlFile = File(projectRoot, "preview.html")
val docsPreviewHtmlFile = File(projectRoot, "docs/asr10/generator/preview.html")

saveOutputFile(internalLayFile, mameXml, "MAME internal layout")
saveOutputFile(artworkLayFile, mameXml, "MAME artwork layout")
saveOutputFile(previewHtmlFile, svgHtml, "HTML preview")
saveOutputFile(docsPreviewHtmlFile, svgHtml, "Generator HTML preview")

val annCount = primitives.filterIsInstance<Primitive.AnnunciatorWord>().size
val btnCount = primitives.filterIsInstance<Primitive.ButtonInstance>().size
val totalCount = primitives.size
println("📊 Genereringsstatistik: $totalCount element ($btnCount knappar, $annCount annunciator-ord)")
println("✨ Pipeline körd: Layout och rendering slutförd med 100% paritet!")
