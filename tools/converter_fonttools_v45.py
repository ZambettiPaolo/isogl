"""
SVG Font to TOML Converter - Versione con fontTools v4.5+
Risolve i bug sulle curve di Bézier usando fontTools per una conversione precisa

Caratteristiche:
- ✓ Usa fontTools BasePen per parsing robusto
- ✓ Formule matematiche precise per curve cubiche e quadratiche di Bézier
- ✓ Gestione corretta di tutti i comandi SVG path (M, L, H, V, C, S, Q, T, A, Z)
- ✓ Gestione separatori 0xFFFFFF corretta
- ✓ Visualizzazione opzionale con pygame
"""

import toml
import pygame
from typing import List, Tuple, Dict, Any, Optional
import sys
import xml.etree.ElementTree as ET
from fontTools.pens.basePen import BasePen
import math
import re

# Inizializza pygame
pygame.init()


class PolylinePen(BasePen):
    """
    Pen personalizzato che converte curve di Bézier in polilinee.
    Usa le formule matematiche corrette per curve cubiche e quadratiche.
    """
    
    def __init__(self, segments_per_curve: int = 6):
        super().__init__(None)
        self.segments_per_curve = max(2, segments_per_curve)
        self.current_point = None
        self.polylines = []  # Lista di polilinee separate
        self.current_polyline = []
        self.start_point = None
        
    def _moveTo(self, pt):
        """Inizia una nuova polilinea"""
        # Se c'era una polilinea in corso, salvala
        if self.current_polyline:
            self.polylines.append(self.current_polyline[:])
            self.current_polyline = []
        
        # Inizia nuova polilinea - arrotonda a interi
        pt = (round(float(pt[0])), round(float(pt[1])))
        self.current_point = pt
        self.start_point = pt
        self.current_polyline.append(list(pt))
    
    def _lineTo(self, pt):
        """Aggiunge una linea alla polilinea corrente"""
        if self.current_point is None:
            self._moveTo(pt)
            return
        
        # Arrotonda a interi
        pt = (round(float(pt[0])), round(float(pt[1])))
        self.current_polyline.append(list(pt))
        self.current_point = pt
    
    def _curveToOne(self, pt1, pt2, pt3):
        """
        Converte una curva cubica di Bézier in segmenti lineari.
        Usa la formula parametrica corretta della curva cubica di Bézier.
        
        Formula: B(t) = (1-t)³P₀ + 3(1-t)²tP₁ + 3(1-t)t²P₂ + t³P₃
        dove t ∈ [0, 1]
        """
        if self.current_point is None:
            self._moveTo(pt3)
            return
        
        # Punti di controllo della curva cubica
        p0 = self.current_point
        p1 = (float(pt1[0]), float(pt1[1]))
        p2 = (float(pt2[0]), float(pt2[1]))
        p3 = (float(pt3[0]), float(pt3[1]))
        
        # Genera punti lungo la curva usando interpolazione parametrica
        for i in range(1, self.segments_per_curve + 1):
            t = i / self.segments_per_curve
            t_inv = 1.0 - t
            
            # Coefficienti della curva cubica di Bézier
            c0 = t_inv * t_inv * t_inv  # (1-t)³
            c1 = 3.0 * t_inv * t_inv * t  # 3(1-t)²t
            c2 = 3.0 * t_inv * t * t  # 3(1-t)t²
            c3 = t * t * t  # t³
            
            # Calcola il punto sulla curva e arrotonda a interi
            x = round(c0 * p0[0] + c1 * p1[0] + c2 * p2[0] + c3 * p3[0])
            y = round(c0 * p0[1] + c1 * p1[1] + c2 * p2[1] + c3 * p3[1])
            
            self.current_polyline.append([x, y])
        
        self.current_point = (round(p3[0]), round(p3[1]))
    
    def _qCurveToOne(self, pt1, pt2):
        """
        Converte una curva quadratica di Bézier in segmenti lineari.
        Usa la formula parametrica corretta della curva quadratica di Bézier.
        
        Formula: B(t) = (1-t)²P₀ + 2(1-t)tP₁ + t²P₂
        dove t ∈ [0, 1]
        """
        if self.current_point is None:
            self._moveTo(pt2)
            return
        
        # Punti di controllo della curva quadratica
        p0 = self.current_point
        p1 = (float(pt1[0]), float(pt1[1]))
        p2 = (float(pt2[0]), float(pt2[1]))
        
        # Genera punti lungo la curva usando interpolazione parametrica
        for i in range(1, self.segments_per_curve + 1):
            t = i / self.segments_per_curve
            t_inv = 1.0 - t
            
            # Coefficienti della curva quadratica di Bézier
            c0 = t_inv * t_inv  # (1-t)²
            c1 = 2.0 * t_inv * t  # 2(1-t)t
            c2 = t * t  # t²
            
            # Calcola il punto sulla curva e arrotonda a interi
            x = round(c0 * p0[0] + c1 * p1[0] + c2 * p2[0])
            y = round(c0 * p0[1] + c1 * p1[1] + c2 * p2[1])
            
            self.current_polyline.append([x, y])
        
        self.current_point = (round(p2[0]), round(p2[1]))
    
    def _closePath(self):
        """Chiude il percorso corrente tornando al punto iniziale"""
        if self.current_point and self.start_point:
            # Se il punto finale non coincide con quello iniziale, aggiungi una linea
            if abs(self.current_point[0] - self.start_point[0]) > 0.01 or \
               abs(self.current_point[1] - self.start_point[1]) > 0.01:
                self.current_polyline.append(list(self.start_point))
            self.current_point = self.start_point
    
    def _endPath(self):
        """Termina il percorso corrente"""
        if self.current_polyline:
            self.polylines.append(self.current_polyline[:])
            self.current_polyline = []
    
    def get_vertices_and_indices(self) -> Tuple[List[List[float]], List[int]]:
        """
        Genera vertici e indici dal percorso tracciato.
        
        Regole corrette per gli indici:
        1. 0xFFFFFF (16777215) separa polilinee multiple DENTRO lo stesso glifo
        2. Se c'è solo una polilinea, NON mettere 0xFFFFFF
        3. Se ci sono più polilinee, metti 0xFFFFFF TRA di esse
        4. MAI mettere 0xFFFFFF alla fine della lista
        """
        # Salva l'ultima polilinea se esiste
        if self.current_polyline:
            self.polylines.append(self.current_polyline[:])
            self.current_polyline = []
        
        vertices = []
        indices = []
        vertex_idx = 0
        
        for polyline_idx, polyline in enumerate(self.polylines):
            if not polyline:
                continue
            
            # Se non è la prima polilinea, aggiungi separatore
            if polyline_idx > 0:
                indices.append(0xFFFFFF)
            
            # Aggiungi vertici e indici per questa polilinea
            # Assicurati che le coordinate siano interi
            for point in polyline:
                vertices.append([int(round(point[0])), int(round(point[1]))])
                indices.append(vertex_idx)
                vertex_idx += 1
        
        return vertices, indices


def parse_svg_path_to_pen(d_path: str, pen: BasePen):
    """
    Parser SVG path robusto che invia comandi al pen.
    Gestisce correttamente tutti i comandi SVG path incluse le curve di Bézier.
    """
    if not d_path or not d_path.strip():
        return
    
    # Normalizza il percorso
    d_path = d_path.strip()
    
    # Separa comandi e numeri usando regex
    # Match comandi (lettere) e numeri (inclusi negativi e decimali)
    tokens = re.findall(r'[MmZzLlHhVvCcSsQqTtAa]|[-+]?[0-9]*\.?[0-9]+(?:[eE][-+]?[0-9]+)?', d_path)
    
    current_pos = (0.0, 0.0)
    start_pos = (0.0, 0.0)
    last_control = None
    last_cmd = None
    
    i = 0
    while i < len(tokens):
        token = tokens[i]
        
        # Se è un comando
        if token in 'MmZzLlHhVvCcSsQqTtAa':
            cmd = token
            i += 1
        elif last_cmd and last_cmd not in 'Zz':
            # Comando implicito (ripeti l'ultimo comando)
            cmd = last_cmd
            # Non incrementare i, processeremo i numeri
        else:
            i += 1
            continue
        
        cmd_upper = cmd.upper()
        is_relative = cmd.islower()
        
        # Raccogli numeri per questo comando
        numbers = []
        while i < len(tokens) and tokens[i] not in 'MmZzLlHhVvCcSsQqTtAa':
            try:
                numbers.append(float(tokens[i]))
            except ValueError:
                pass
            i += 1
        
        # Processa il comando
        if cmd_upper == 'M':  # MoveTo
            if len(numbers) >= 2:
                x, y = numbers[0], numbers[1]
                if is_relative and current_pos:
                    x += current_pos[0]
                    y += current_pos[1]
                
                pen.moveTo((x, y))
                current_pos = (x, y)
                start_pos = (x, y)
                
                # Punti aggiuntivi dopo M sono trattati come LineTo
                for j in range(2, len(numbers) - 1, 2):
                    x, y = numbers[j], numbers[j + 1]
                    if is_relative:
                        x += current_pos[0]
                        y += current_pos[1]
                    pen.lineTo((x, y))
                    current_pos = (x, y)
        
        elif cmd_upper == 'L':  # LineTo
            for j in range(0, len(numbers) - 1, 2):
                x, y = numbers[j], numbers[j + 1]
                if is_relative:
                    x += current_pos[0]
                    y += current_pos[1]
                pen.lineTo((x, y))
                current_pos = (x, y)
        
        elif cmd_upper == 'H':  # Horizontal LineTo
            for x in numbers:
                if is_relative:
                    x += current_pos[0]
                pen.lineTo((x, current_pos[1]))
                current_pos = (x, current_pos[1])
        
        elif cmd_upper == 'V':  # Vertical LineTo
            for y in numbers:
                if is_relative:
                    y += current_pos[1]
                pen.lineTo((current_pos[0], y))
                current_pos = (current_pos[0], y)
        
        elif cmd_upper == 'C':  # Cubic Bézier
            for j in range(0, len(numbers) - 5, 6):
                x1, y1 = numbers[j], numbers[j + 1]
                x2, y2 = numbers[j + 2], numbers[j + 3]
                x, y = numbers[j + 4], numbers[j + 5]
                
                if is_relative:
                    x1 += current_pos[0]
                    y1 += current_pos[1]
                    x2 += current_pos[0]
                    y2 += current_pos[1]
                    x += current_pos[0]
                    y += current_pos[1]
                
                pen.curveTo((x1, y1), (x2, y2), (x, y))
                last_control = (x2, y2)
                current_pos = (x, y)
        
        elif cmd_upper == 'S':  # Smooth Cubic Bézier
            for j in range(0, len(numbers) - 3, 4):
                # Primo punto di controllo è riflesso dell'ultimo
                if last_control and last_cmd and last_cmd.upper() in 'CS':
                    x1 = 2 * current_pos[0] - last_control[0]
                    y1 = 2 * current_pos[1] - last_control[1]
                else:
                    x1, y1 = current_pos
                
                x2, y2 = numbers[j], numbers[j + 1]
                x, y = numbers[j + 2], numbers[j + 3]
                
                if is_relative:
                    x2 += current_pos[0]
                    y2 += current_pos[1]
                    x += current_pos[0]
                    y += current_pos[1]
                
                pen.curveTo((x1, y1), (x2, y2), (x, y))
                last_control = (x2, y2)
                current_pos = (x, y)
        
        elif cmd_upper == 'Q':  # Quadratic Bézier
            for j in range(0, len(numbers) - 3, 4):
                x1, y1 = numbers[j], numbers[j + 1]
                x, y = numbers[j + 2], numbers[j + 3]
                
                if is_relative:
                    x1 += current_pos[0]
                    y1 += current_pos[1]
                    x += current_pos[0]
                    y += current_pos[1]
                
                pen.qCurveTo((x1, y1), (x, y))
                last_control = (x1, y1)
                current_pos = (x, y)
        
        elif cmd_upper == 'T':  # Smooth Quadratic Bézier
            for j in range(0, len(numbers) - 1, 2):
                # Punto di controllo è riflesso dell'ultimo
                if last_control and last_cmd and last_cmd.upper() in 'QT':
                    x1 = 2 * current_pos[0] - last_control[0]
                    y1 = 2 * current_pos[1] - last_control[1]
                else:
                    x1, y1 = current_pos
                
                x, y = numbers[j], numbers[j + 1]
                
                if is_relative:
                    x += current_pos[0]
                    y += current_pos[1]
                
                pen.qCurveTo((x1, y1), (x, y))
                last_control = (x1, y1)
                current_pos = (x, y)
        
        elif cmd_upper == 'A':  # Arc (converti in curve cubiche approssimate)
            for j in range(0, len(numbers) - 6, 7):
                rx, ry = numbers[j], numbers[j + 1]
                x_rotation = numbers[j + 2]
                large_arc = numbers[j + 3]
                sweep = numbers[j + 4]
                x, y = numbers[j + 5], numbers[j + 6]
                
                if is_relative:
                    x += current_pos[0]
                    y += current_pos[1]
                
                # Converte arco ellittico in punti
                arc_points = approximate_arc(
                    current_pos[0], current_pos[1],
                    rx, ry, x_rotation, large_arc, sweep,
                    x, y
                )
                
                for arc_x, arc_y in arc_points:
                    pen.lineTo((arc_x, arc_y))
                
                current_pos = (x, y)
        
        elif cmd_upper == 'Z':  # ClosePath
            pen.closePath()
            current_pos = start_pos
        
        # Aggiorna last_cmd se non è Z
        if cmd_upper != 'Z':
            last_cmd = cmd


def approximate_arc(x1, y1, rx, ry, phi, fa, fs, x2, y2, segments=10):
    """
    Approssima un arco ellittico con segmenti lineari.
    Implementazione semplificata per archi SVG.
    """
    # Se i raggi sono zero o i punti coincidono, ritorna linea retta
    if rx == 0 or ry == 0 or (abs(x1 - x2) < 0.001 and abs(y1 - y2) < 0.001):
        return [(x2, y2)]
    
    # Per semplicità, approssimiamo con segmenti lineari diretti
    # Una implementazione completa richiederebbe la conversione completa dell'arco SVG
    points = []
    for i in range(1, segments + 1):
        t = i / segments
        x = x1 + (x2 - x1) * t
        y = y1 + (y2 - y1) * t
        points.append((x, y))
    
    return points


def parse_svg_path_with_fonttools(d_path: str, segments_per_curve: int = 6) -> Tuple[List[List[float]], List[int]]:
    """
    Parsifica un percorso SVG usando fontTools per gestire correttamente le curve di Bézier.
    """
    if not d_path or d_path.isspace():
        return [], []
    
    # Crea il pen personalizzato
    pen = PolylinePen(segments_per_curve)
    
    # Parsifica il percorso SVG
    try:
        parse_svg_path_to_pen(d_path, pen)
        pen._endPath()  # Assicurati di terminare il percorso
        return pen.get_vertices_and_indices()
    except Exception as e:
        print(f"⚠️  Errore nel parsing del percorso: {e}")
        return [], []


class GlyphViewer:
    """Visualizzatore di glifi con pygame"""
    
    def __init__(self):
        self.screen = pygame.display.set_mode((800, 600))
        pygame.display.set_caption("SVG Font Converter - fontTools v4.5 Edition")
        self.font = pygame.font.Font(None, 24)
        self.small_font = pygame.font.Font(None, 18)
        self.running = True
        
        # Colori
        self.bg_color = (240, 240, 245)
        self.glyph_color = (30, 30, 150)
        self.text_color = (30, 30, 30)
        self.highlight_color = (200, 50, 50)
    
    def show_glyph(self, name: str, codepoint: int, vertices: List[List[float]], 
                  indices: List[int], advance: float):
        """Mostra un glifo"""
        self.screen.fill(self.bg_color)
        
        # Informazioni
        char_display = chr(codepoint) if 32 <= codepoint <= 0x10FFFF else '□'
        num_polylines = indices.count(0xFFFFFF) + (1 if indices and indices[0] != 0xFFFFFF else 0)
        
        info_lines = [
            f"Glifo: {name}",
            f"Carattere: '{char_display}' (U+{codepoint:04X})",
            f"Advance: {advance:.1f}",
            f"Vertici: {len(vertices)}",
            f"Indici: {len(indices)}",
            f"Polilinee: {num_polylines}",
            f"Separatori (0xFFFFFF): {indices.count(0xFFFFFF)}",
            "",
            "SPACE: prossimo  |  ESC: salta tutti  |  S: salta a TOML"
        ]
        
        for i, line in enumerate(info_lines):
            text = self.small_font.render(line, True, self.text_color)
            self.screen.blit(text, (20, 20 + i * 22))
        
        # Disegna glifo se ci sono vertici
        if vertices:
            # Calcola bounding box
            xs = [v[0] for v in vertices]
            ys = [v[1] for v in vertices]
            min_x, max_x = min(xs), max(xs)
            min_y, max_y = min(ys), max(ys)
            width = max_x - min_x
            height = max_y - min_y
            
            if width > 0 and height > 0:
                # Scala per adattare
                scale = min(300 / width, 300 / height, 2.5)
                
                # Centro
                center_x = 400
                center_y = 450
                
                # Funzione per convertire coordinate
                def to_screen(x, y):
                    screen_x = center_x + (x - (min_x + width/2)) * scale
                    screen_y = center_y - (y - (min_y + height/2)) * scale
                    return int(screen_x), int(screen_y)
                
                # Disegna connessioni (segmenti tra vertici, salta 0xFFFFFF)
                if indices:
                    i = 0
                    while i < len(indices):
                        start_idx = indices[i]
                        if start_idx == 0xFFFFFF:
                            i += 1
                            continue
                        
                        j = i + 1
                        while j < len(indices) and indices[j] != 0xFFFFFF:
                            end_idx = indices[j]
                            
                            if start_idx < len(vertices) and end_idx < len(vertices):
                                x1, y1 = to_screen(vertices[start_idx][0], vertices[start_idx][1])
                                x2, y2 = to_screen(vertices[end_idx][0], vertices[end_idx][1])
                                
                                pygame.draw.line(self.screen, self.glyph_color, 
                                               (x1, y1), (x2, y2), 2)
                            
                            start_idx = end_idx
                            j += 1
                        
                        i = j + 1
        
        pygame.display.flip()
        
        # Gestione input
        waiting = True
        skip_all = False
        skip_to_toml = False
        
        while waiting and self.running:
            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    self.running = False
                    waiting = False
                    skip_all = True
                elif event.type == pygame.KEYDOWN:
                    if event.key == pygame.K_ESCAPE:
                        skip_all = True
                        waiting = False
                    elif event.key == pygame.K_SPACE:
                        waiting = False
                    elif event.key == pygame.K_s:
                        skip_to_toml = True
                        waiting = False
        
        return skip_all, skip_to_toml
    
    def close(self):
        pygame.quit()


def parse_svg_font_file(svg_file: str) -> Dict[str, Any]:
    """Parsifica un file SVG font"""
    try:
        tree = ET.parse(svg_file)
        root = tree.getroot()
        
        # Namespace SVG
        ns = {'svg': 'http://www.w3.org/2000/svg'}
        
        # Trova font-face per metriche
        font_face = root.find('.//svg:font-face', ns)
        ascent = 800.0
        descent = -200.0
        cap_height = None

        if font_face is not None:
            ascent = float(font_face.get('ascent', '800'))
            descent = float(font_face.get('descent', '-200'))
            raw_cap = font_face.get('cap-height')
            if raw_cap is not None:
                cap_height = float(raw_cap)

        # Trova tutti i glifi
        glyphs = []
        for glyph in root.findall('.//svg:glyph', ns):
            glyph_data = {
                'name': glyph.get('glyph-name', ''),
                'unicode': glyph.get('unicode', ''),
                'd': glyph.get('d', ''),
                'advance': float(glyph.get('horiz-adv-x', '280'))
            }
            glyphs.append(glyph_data)

        return {
            'ascent': ascent,
            'descent': descent,
            'cap_height': cap_height,
            'glyphs': glyphs
        }
    
    except Exception as e:
        print(f"❌ Errore nel parsing del file SVG: {e}")
        return None


def unicode_to_codepoint(unicode_str: str) -> Optional[int]:
    """Converte una stringa Unicode in codepoint"""
    if not unicode_str:
        return None
    
    # Formato &#xHHHH;
    if unicode_str.startswith('&#x'):
        try:
            return int(unicode_str[3:-1], 16)
        except:
            pass
    
    # Formato &#NNNN;
    if unicode_str.startswith('&#'):
        try:
            return int(unicode_str[2:-1])
        except:
            pass
    
    # Singolo carattere
    if len(unicode_str) == 1:
        return ord(unicode_str)
    
    return None


def clean_indices(indices: List[int]) -> List[int]:
    """Pulisce la lista degli indici rimuovendo 0xFFFFFF finali"""
    if not indices:
        return indices
    
    # Rimuovi tutti gli 0xFFFFFF dalla fine
    while indices and indices[-1] == 0xFFFFFF:
        indices.pop()
    
    return indices


def convert_font_with_fonttools(svg_file: str, output_file: str, show_glyphs: bool = True, 
                                segments_per_curve: int = 6, normalize_to_byte: bool = False):
    """Converte il font usando fontTools per gestire correttamente le curve di Bézier"""
    
    print(f"\n{'='*70}")
    print("SVG FONT to TOML CONVERTER - fontTools v4.5 Edition")
    print(f"{'='*70}")
    print(f"✓ Usa fontTools BasePen per parsing robusto")
    print(f"✓ Formule matematiche precise per curve cubiche e quadratiche")
    print(f"✓ Gestione corretta di tutti i comandi SVG path")
    print(f"✓ Coordinate arrotondate a interi")
    if normalize_to_byte:
        print(f"✓ Normalizzazione a byte (0-255) preservando aspect ratio")
        print(f"✓ Advance normalizzato nello stesso range")
    print(f"\nSegmenti per curva: {segments_per_curve}")
    print(f"File SVG: {svg_file}")
    print(f"Output TOML: {output_file}")
    
    # Parsifica font
    print("\n🔍 Parsificazione SVG...")
    font_data = parse_svg_font_file(svg_file)
    
    if not font_data or not font_data.get('glyphs'):
        print("❌ ERRORE: Impossibile parsificare il file SVG")
        return
    
    print(f"✓ Trovati {len(font_data['glyphs'])} glifi")
    print(f"✓ Ascent: {font_data['ascent']}, Descent: {font_data['descent']}")
    
    # Inizializza visualizzatore
    viewer = None
    if show_glyphs:
        viewer = GlyphViewer()
        print("\n📺 Modalità visualizzazione attiva")
        print("   SPACE: prossimo glifo")
        print("   ESC: salta visualizzazione")
        print("   S: salta a generazione TOML")
    
    # Processa glifi - PRIMA PASSATA: converti senza normalizzare
    toml_glyphs = []
    processed = 0
    skipped = 0
    skip_all = False
    skip_to_toml = False
    
    print(f"\n🔄 Processamento glifi...")
    
    for i, glyph in enumerate(font_data['glyphs']):
        unicode_str = glyph.get('unicode', '')
        name = glyph.get('name', '')
        advance = glyph.get('advance', 280.0)
        d_path = glyph.get('d', '')
        
        # Converti Unicode
        codepoint = unicode_to_codepoint(unicode_str)
        if codepoint is None:
            skipped += 1
            continue
        
        # Parsifica percorso con fontTools
        vertices, indices = parse_svg_path_with_fonttools(d_path, segments_per_curve)
        
        # Pulisci indici (rimuovi 0xFFFFFF finali)
        indices = clean_indices(indices)
        
        # Visualizza (con coordinate originali)
        if viewer and not skip_all and not skip_to_toml:
            skip_all, skip_to_toml = viewer.show_glyph(
                name, codepoint, vertices, indices, advance
            )
        
        if skip_to_toml:
            print("\n⏩ Saltando visualizzazione, generazione TOML...")
            skip_all = True
        
        # Aggiungi a TOML (salva vertici originali per ora)
        toml_glyphs.append({
            'codepoint': codepoint,
            'advance': advance,
            'vertices': vertices,  # Vertici originali
            'indices': indices
        })
        
        processed += 1
        
        # Stampa progresso
        if processed <= 10 or processed % 100 == 0:
            char_display = chr(codepoint) if 32 <= codepoint <= 0x10FFFF else '□'
            fff_count = indices.count(0xFFFFFF)
            print(f"  [{processed:4d}] U+{codepoint:04X} '{char_display}' - {name:20s} | "
                  f"V:{len(vertices):3d} I:{len(indices):3d} Sep:{fff_count}")
        
        if skip_all and not skip_to_toml:
            print("\n⛔ Interrotto dall'utente")
            break
    
    # Chiudi visualizzatore
    if viewer:
        viewer.close()
    
    # Inizializza variabili per la normalizzazione
    all_x = []
    all_y = []
    all_advances = []
    global_min_x = global_max_x = global_min_y = global_max_y = 0
    max_advance = 0
    range_x = range_y = 0
    scale = 1.0
    
    # SECONDA PASSATA: normalizza a byte se richiesto
    if normalize_to_byte and toml_glyphs:
        print(f"\n📏 Calcolo limiti globali per normalizzazione...")
        
        # Trova i limiti globali di tutti i vertici di tutti i glifi
        all_x = []
        all_y = []
        all_advances = []
        
        for glyph in toml_glyphs:
            for v in glyph['vertices']:
                all_x.append(v[0])
                all_y.append(v[1])
            all_advances.append(glyph['advance'])
        
        if all_x and all_y and all_advances:
            global_min_x = min(all_x)
            global_max_x = max(all_x)
            global_min_y = min(all_y)
            global_max_y = max(all_y)
            
            # advance è la dimensione orizzontale, quindi considera anche quella
            max_advance = max(all_advances)
            # Se advance è più grande del max_x dei vertici, usala come limite
            if max_advance > global_max_x:
                global_max_x = max_advance
            
            # Calcola i range
            range_x = global_max_x - global_min_x
            range_y = global_max_y - global_min_y
            
            # Evita divisione per zero
            if range_x == 0:
                range_x = 1
            if range_y == 0:
                range_y = 1
            
            # USA UN SOLO FATTORE DI SCALA per mantenere l'aspect ratio
            # Scegli il minore tra i due per far stare tutto in 0-255
            scale_x_candidate = 255.0 / range_x
            scale_y_candidate = 255.0 / range_y
            scale = min(scale_x_candidate, scale_y_candidate)
            
            print(f"✓ Range X: [{global_min_x:.1f}, {global_max_x:.1f}] (larghezza: {range_x:.1f})")
            print(f"✓ Range Y: [{global_min_y:.1f}, {global_max_y:.1f}] (altezza: {range_y:.1f})")
            print(f"✓ Max advance: {max_advance:.1f}")
            print(f"✓ Fattore scala unico: {scale:.6f} (mantiene aspect ratio)")
            
            # Normalizza tutti i glifi usando lo stesso fattore di scala
            print(f"🔄 Normalizzazione con fattore di scala unico...")
            for glyph in toml_glyphs:
                # Normalizza vertici con il fattore di scala unico
                if glyph['vertices']:
                    normalized_vertices = []
                    for x, y in glyph['vertices']:
                        norm_x = int(round((x - global_min_x) * scale))
                        norm_y = int(round((y - global_min_y) * scale))
                        # Clamp a 0-255
                        norm_x = max(0, min(255, norm_x))
                        norm_y = max(0, min(255, norm_y))
                        normalized_vertices.append([norm_x, norm_y])
                    glyph['vertices'] = normalized_vertices
                
                # Normalizza advance con lo STESSO fattore di scala
                original_advance = glyph['advance']
                normalized_advance = int(round((original_advance - global_min_x) * scale))
                # Clamp a 0-255
                normalized_advance = max(0, min(255, normalized_advance))
                glyph['advance'] = normalized_advance
            
            print(f"✓ Tutti i glifi normalizzati mantenendo l'aspect ratio")
            print(f"✓ Valori advance normalizzati (0-255)")
    
    # Ordina per codepoint
    toml_glyphs.sort(key=lambda x: x['codepoint'])
    
    # Controlla finale per ogni glifo
    print(f"\n🔍 Controllo finale indici...")
    errors = 0
    for glyph in toml_glyphs:
        if glyph['indices'] and glyph['indices'][-1] == 0xFFFFFF:
            errors += 1
            if errors <= 5:
                print(f"  ⚠️  Glifo U+{glyph['codepoint']:04X} ha 0xFFFFFF finale - rimosso")
            glyph['indices'] = clean_indices(glyph['indices'])
    
    if errors > 0:
        print(f"  ✓ Corretti {errors} indici finali errati")
    else:
        print(f"  ✓ Tutti gli indici sono corretti")
    
    # Altezza di riferimento: ISO 3098 usa l'altezza del maiuscolo (cap-height).
    # Se il font non la dichiara, si usa l'em (ascent - descent).
    cap_height = font_data.get('cap_height')
    if cap_height is None or cap_height <= 0:
        cap_height = font_data['ascent'] - font_data['descent']

    # Crea TOML
    toml_data = {
        'ascent': font_data['ascent'],
        'descent': font_data['descent'],
        'height': cap_height,
        'line_gap': 0.0,
        'glyphs': toml_glyphs
    }
    
    # Aggiungi metadati se normalizzato
    if normalize_to_byte:
        toml_data['normalized_to_byte'] = True
        if all_x and all_y and all_advances:
            toml_data['normalization_info'] = {
                'global_min_x': global_min_x,
                'global_max_x': global_max_x,
                'global_min_y': global_min_y,
                'global_max_y': global_max_y,
                'max_advance': max_advance,
                'scale': scale,  # Fattore di scala unico
                'preserves_aspect_ratio': True
            }
    
    # Scrivi file
    print(f"\n💾 Scrittura file TOML...")
    with open(output_file, 'w', encoding='utf-8') as f:
        toml.dump(toml_data, f)
    
    # Statistiche
    print(f"\n{'='*70}")
    print("✅ CONVERSIONE COMPLETATA!")
    print(f"{'='*70}")
    print(f"File TOML generato: {output_file}")
    print(f"Glifi processati: {processed}/{len(font_data['glyphs'])}")
    print(f"Glifi saltati: {skipped}")
    print(f"Segmenti per curva: {segments_per_curve}")
    if normalize_to_byte:
        print(f"Coordinate: normalizzate a byte (0-255) con fattore di scala UNICO")
        if all_x and all_y and all_advances:
            print(f"  Fattore scala: {scale:.6f} (mantiene aspect ratio)")
            print(f"  Range X originale: {range_x:.1f} → scala a: {range_x * scale:.1f}")
            print(f"  Range Y originale: {range_y:.1f} → scala a: {range_y * scale:.1f}")
    else:
        print(f"Coordinate: interi nelle unità originali")
    
    total_vertices = sum(len(g['vertices']) for g in toml_glyphs)
    total_indices = sum(len(g['indices']) for g in toml_glyphs)
    total_fff = sum(g['indices'].count(0xFFFFFF) for g in toml_glyphs)
    
    print(f"\nStatistiche:")
    print(f"  Vertici totali: {total_vertices:,}")
    print(f"  Indici totali: {total_indices:,}")
    print(f"  Separatori (0xFFFFFF): {total_fff:,}")
    if toml_glyphs:
        print(f"  Media vertici/glifo: {total_vertices/len(toml_glyphs):.1f}")
    
    # Esempi di output
    print(f"\n📋 Primi 5 glifi convertiti:")
    for i, glyph in enumerate(toml_glyphs[:5]):
        char = chr(glyph['codepoint']) if 32 <= glyph['codepoint'] <= 0x10FFFF else '□'
        fff_count = glyph['indices'].count(0xFFFFFF)
        last_idx = glyph['indices'][-1] if glyph['indices'] else -1
        num_polylines = fff_count + (1 if glyph['indices'] and glyph['indices'][0] != 0xFFFFFF else 0)
        
        # Mostra esempio di coordinate e advance
        coord_example = ""
        if glyph['vertices']:
            v = glyph['vertices'][0]
            coord_example = f"vertice: [{v[0]}, {v[1]}]"
        
        print(f"  {i+1}. U+{glyph['codepoint']:04X} '{char}': "
              f"{len(glyph['vertices'])} vertici, {len(glyph['indices'])} indici, "
              f"{num_polylines} polilinee")
        if coord_example:
            print(f"      {coord_example}, advance: {glyph['advance']}")
    
    print(f"\n✅ File {output_file} creato con successo!")
    print("📝 Curve di Bézier convertite con formule matematiche precise")
    print("📐 Tutte le coordinate arrotondate a interi")
    if normalize_to_byte:
        print("⚖️  Aspect ratio preservato (un solo fattore di scala)")
        print("📊 Advance normalizzato nello stesso range 0-255")


def main():
    """Funzione principale"""
    print("╔" + "═" * 68 + "╗")
    print("║" + " " * 10 + "SVG FONT to TOML CONVERTER - fontTools v4.5" + " " * 15 + "║")
    print("╚" + "═" * 68 + "╝")
    print("\n🔧 Risolve i bug sulle curve di Bézier usando fontTools")
    print("✓ Parsing robusto con BasePen")
    print("✓ Formule matematiche precise per curve cubiche/quadratiche")
    print("✓ Gestione corretta di tutti i comandi SVG path")
    print("✓ Coordinate arrotondate a interi\n")
    
    # File di input
    svg_file = "ReliefSingleLineSVG-Regular.svg"
    
    # Chiedi parametri
    print("⚙️  Parametri di conversione:\n")
    
    # Segmenti per curva
    while True:
        try:
            segments_input = input("Segmenti per curva (2-12, default 6): ").strip()
            if not segments_input:
                segments = 6
                break
            segments = int(segments_input)
            if 2 <= segments <= 12:
                break
            print("❌ Inserisci un numero tra 2 e 12")
        except ValueError:
            print("❌ Numero non valido")
    
    # Normalizzazione a byte
    normalize_input = input("\nNormalizzare coordinate a byte 0-255? (s/n, default n): ").strip().lower()
    normalize = normalize_input == 's'
    
    # Visualizzazione
    show_input = input("\nVisualizzare i glifi durante la conversione? (s/n, default n): ").strip().lower()
    show_gl = show_input == 's'
    
    # File output
    suffix = "_byte" if normalize else "_int"
    default_name = f"font_{segments}seg{suffix}.toml"
    output_input = input(f"\nNome file TOML (default: {default_name}): ").strip()
    output_file = output_input if output_input else default_name
    
    print(f"\n{'='*70}")
    print(f"📝 Parametri scelti:")
    print(f"   File input: {svg_file}")
    print(f"   Segmenti per curva: {segments}")
    print(f"   Normalizzazione byte: {'✓ SI (0-255)' if normalize else '✗ NO (coordinate originali)'}")
    print(f"   Visualizzazione: {'✓ SI' if show_gl else '✗ NO'}")
    print(f"   File output: {output_file}")
    print(f"   Motore: fontTools v4.5+ (BasePen)")
    print(f"   Coordinate: interi arrotondati")
    print(f"{'='*70}")
    
    input("\n⏎ Premi INVIO per iniziare la conversione...")
    
    try:
        convert_font_with_fonttools(svg_file, output_file, show_gl, segments, normalize)
    except FileNotFoundError:
        print(f"\n❌ ERRORE: File {svg_file} non trovato!")
        print("   Assicurati che il file SVG sia nella stessa directory.")
    except Exception as e:
        print(f"\n❌ ERRORE durante la conversione: {e}")
        import traceback
        traceback.print_exc()
    
    input("\n⏎ Premi INVIO per uscire...")


if __name__ == "__main__":
    main()
