#!/usr/bin/env python3
"""
generate_mascot_pages.py
Generates index-a.html, index-b.html, index-c.html and updates index.html
featuring 'Lil Cube' (Nintendo-Kirby-inspired Ice Cube Primitive mascot).
"""

import os
import re

ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INDEX_PATH = os.path.join(ROOT_DIR, "index.html")

# Read original base index.html
with open(INDEX_PATH, "r", encoding="utf-8") as f:
    orig_html = f.read()

# Strip any existing prototype switcher or injected mascot code if re-running
clean_html = re.sub(r'<!-- Mascot Experience Prototype Switcher Bar -->.*?</div>\s*</div>\s*', '', orig_html, flags=re.DOTALL)
clean_html = re.sub(r'/\* Mascot Prototype Switcher Bar \*/.*?(?=</style>)', '', clean_html, flags=re.DOTALL)
clean_html = re.sub(r'/\* Option [ABC]:.*?Styles \*/.*?(?=</style>)', '', clean_html, flags=re.DOTALL)
clean_html = re.sub(r'<!-- Option C: Interactive Lil Cube Mascot Companion Stage -->.*?</div>\s*</div>\s*</div>', '', clean_html, flags=re.DOTALL)
clean_html = re.sub(r'<div class="companion-dashboard">.*?</div>', '', clean_html, flags=re.DOTALL)
clean_html = re.sub(r'<!-- Script for Option [ABC]:.*?script>', '', clean_html, flags=re.DOTALL)

def get_switcher_bar(active_opt):
    a_act = "active" if active_opt == "a" else ""
    b_act = "active" if active_opt == "b" else ""
    c_act = "active" if active_opt == "c" else ""
    main_act = "active" if active_opt == "main" else ""
    return f"""
    <!-- Mascot Experience Prototype Switcher Bar -->
    <div id="mascotPrototypeBar" class="mascot-prototype-bar">
        <div class="mascot-bar-container">
            <span class="mascot-bar-tag">🧊 LIL CUBE MASCOT:</span>
            <a href="index.html" class="mascot-bar-link {c_act or main_act}">Option C: Interactive Companion</a>
            <a href="index-a.html" class="mascot-bar-link {a_act}">Option A: 聖なるホップ束 (Hopf Saint Aura) ★</a>
            <a href="index-b.html" class="mascot-bar-link {b_act}">Option B: Cosmic Flight</a>
        </div>
    </div>
"""

SHARED_SWITCHER_CSS = """
        /* Mascot Prototype Switcher Bar */
        .mascot-prototype-bar {
            position: sticky;
            top: 0;
            z-index: 1020;
            background: linear-gradient(90deg, #ff2d95 0%, #c026d3 30%, #008080 65%, #0057b7 100%);
            padding: 8px 16px;
            box-shadow: 0 4px 20px rgba(255, 45, 149, 0.3);
            backdrop-filter: blur(12px);
            border-bottom: 1.5px solid rgba(255, 255, 255, 0.4);
        }
        .mascot-bar-container {
            max-width: 1380px;
            margin: 0 auto;
            display: flex;
            align-items: center;
            justify-content: center;
            gap: 10px;
            flex-wrap: wrap;
            font-size: 0.86rem;
            font-weight: 600;
        }
        .mascot-bar-tag {
            color: #ffffff;
            letter-spacing: 0.06em;
            font-size: 0.82rem;
            text-transform: uppercase;
            font-weight: 800;
            text-shadow: 0 1px 4px rgba(0,0,0,0.3);
            display: flex;
            align-items: center;
            gap: 4px;
        }
        .mascot-bar-link {
            color: rgba(255, 255, 255, 0.95);
            text-decoration: none;
            padding: 4px 15px;
            border-radius: 999px;
            background: rgba(255, 255, 255, 0.18);
            border: 1px solid rgba(255, 255, 255, 0.4);
            transition: all 0.25s cubic-bezier(0.34, 1.56, 0.64, 1);
            white-space: nowrap;
            display: inline-flex;
            align-items: center;
            gap: 6px;
        }
        .mascot-bar-link:hover {
            background: #ffffff;
            color: #d90b72;
            transform: translateY(-2px) scale(1.05);
            box-shadow: 0 6px 18px rgba(0, 0, 0, 0.2);
        }
        .mascot-bar-link.active {
            background: #ffffff;
            color: #d90b72;
            font-weight: 800;
            box-shadow: 0 4px 16px rgba(255, 45, 149, 0.45);
            border-color: #ffffff;
        }
        nav.sticky {
            top: 45px;
        }
"""

# ==============================================================================
# OPTION C: INTERACTIVE LIL CUBE COMPANION (SELECTED DIRECTION)
# ==============================================================================
CSS_C = SHARED_SWITCHER_CSS + """
        /* Option C: Lil Cube Interactive Companion Styles */
        header {
            position: relative;
            overflow: visible;
            min-height: 94vh;
            display: flex;
            align-items: center;
            background: radial-gradient(circle at 50% 20%, rgba(255, 240, 248, 0.7) 0%, rgba(240, 232, 255, 0.4) 60%, transparent 100%);
        }

        /* Mascot Stage Perched on Lead Box */
        .companion-stage {
            display: flex;
            align-items: flex-end;
            justify-content: flex-end;
            gap: 1.5rem;
            margin-bottom: -18px;
            padding-right: 2rem;
            position: relative;
            z-index: 15;
        }

        @media (max-width: 820px) {
            .companion-stage {
                flex-direction: column-reverse;
                align-items: center;
                justify-content: center;
                padding-right: 0;
                margin-bottom: 0.8rem;
                gap: 0.8rem;
            }
        }

        /* Nintendo-style Dynamic Speech Bubble */
        .companion-dialogue-bubble {
            background: #ffffff;
            border: 2.5px solid #008080;
            border-radius: 20px;
            padding: 1.1rem 1.5rem;
            max-width: 480px;
            position: relative;
            box-shadow: 0 14px 35px rgba(0, 128, 128, 0.18), 0 4px 12px rgba(255, 45, 149, 0.15);
            cursor: pointer;
            transition: all 0.28s cubic-bezier(0.34, 1.56, 0.64, 1);
            user-select: none;
        }

        .companion-dialogue-bubble:hover {
            transform: translateY(-4px) scale(1.02);
            border-color: #ff2d95;
            box-shadow: 0 18px 40px rgba(255, 45, 149, 0.25);
        }

        .companion-dialogue-bubble::after {
            content: '';
            position: absolute;
            bottom: 22px;
            right: -14px;
            border-width: 10px 0 10px 14px;
            border-style: solid;
            border-color: transparent transparent transparent #ffffff;
            filter: drop-shadow(2px 0 0 #008080);
        }

        @media (max-width: 820px) {
            .companion-dialogue-bubble::after {
                bottom: -14px;
                right: 50%;
                transform: translateX(50%);
                border-width: 14px 10px 0 10px;
                border-color: #ffffff transparent transparent transparent;
                filter: drop-shadow(0 2px 0 #008080);
            }
        }

        .bubble-header {
            display: flex;
            align-items: center;
            justify-content: space-between;
            font-size: 0.78rem;
            font-weight: 800;
            color: #008080;
            text-transform: uppercase;
            letter-spacing: 0.05em;
            margin-bottom: 0.4rem;
        }

        .bubble-content {
            font-size: 1.02rem;
            font-weight: 700;
            color: #1e293b;
            line-height: 1.55;
            min-height: 3em;
            transition: opacity 0.15s ease, transform 0.15s ease;
        }

        .bubble-hint {
            font-size: 0.74rem;
            color: #d90b72;
            margin-top: 0.5rem;
            text-align: right;
            font-weight: 700;
        }

        /* Mascot Sprite Figure */
        .companion-cat-figure {
            position: relative;
            cursor: pointer;
            user-select: none;
            transition: transform 0.25s cubic-bezier(0.34, 1.56, 0.64, 1);
            display: flex;
            flex-direction: column;
            align-items: center;
        }

        .companion-cat-figure:hover {
            transform: translateY(-8px) scale(1.08);
        }

        .companion-cat-figure:active {
            transform: translateY(2px) scale(0.95);
        }

        /* Lil Cube Character Display */
        .companion-cat-img {
            width: 155px;
            height: 155px;
            object-fit: contain;
            display: block;
            filter: drop-shadow(0 14px 28px rgba(0, 128, 128, 0.35)) drop-shadow(0 4px 12px rgba(255, 45, 149, 0.3));
            animation: cubeBobbing 3.2s ease-in-out infinite alternate;
            transform-origin: center bottom;
        }

        @keyframes cubeBobbing {
            0% { transform: translateY(0) scale(1, 1); }
            50% { transform: translateY(-8px) scale(0.98, 1.02); }
            100% { transform: translateY(0) scale(1, 1); }
        }

        .companion-cat-pedestal {
            width: 130px;
            height: 16px;
            background: radial-gradient(ellipse at center, rgba(0, 128, 128, 0.4) 0%, rgba(255, 45, 149, 0.2) 50%, transparent 80%);
            margin: -8px auto 0 auto;
            border-radius: 50%;
            filter: blur(3px);
            animation: pedestalPulse 3.2s ease-in-out infinite alternate;
        }

        @keyframes pedestalPulse {
            0% { transform: scale(1); opacity: 0.8; }
            100% { transform: scale(0.85); opacity: 0.5; }
        }

        /* Companion Control Dashboard */
        .companion-dashboard {
            display: flex;
            align-items: center;
            justify-content: center;
            gap: 12px;
            flex-wrap: wrap;
            margin-top: 1.4rem;
            padding: 8px 18px;
            background: rgba(255, 255, 255, 0.92);
            border-radius: 999px;
            border: 1.5px solid rgba(0, 128, 128, 0.3);
            width: fit-content;
            margin-left: auto;
            margin-right: auto;
            box-shadow: 0 8px 24px rgba(107, 79, 126, 0.1);
        }

        .companion-btn {
            background: #ffffff;
            border: 1.5px solid rgba(0, 128, 128, 0.35);
            color: #334155;
            padding: 6px 18px;
            border-radius: 999px;
            font-size: 0.88rem;
            font-weight: 700;
            cursor: pointer;
            display: inline-flex;
            align-items: center;
            gap: 6px;
            transition: all 0.2s cubic-bezier(0.34, 1.56, 0.64, 1);
        }

        .companion-btn:hover {
            border-color: #ff2d95;
            color: #d90b72;
            transform: translateY(-2px);
            box-shadow: 0 4px 14px rgba(255, 45, 149, 0.25);
        }

        .companion-btn.rainbow-btn {
            background: linear-gradient(135deg, #ff2d95, #c026d3, #008080);
            color: #ffffff;
            border-color: transparent;
            box-shadow: 0 4px 16px rgba(255, 45, 149, 0.35);
        }

        .companion-btn.rainbow-btn:hover {
            transform: translateY(-2px) scale(1.06);
            box-shadow: 0 8px 25px rgba(255, 45, 149, 0.6);
        }

        /* Rainbow Overdrive Active State */
        body.rainbow-overdrive header {
            background: linear-gradient(135deg, rgba(255, 159, 214, 0.3), rgba(162, 237, 255, 0.3), rgba(255, 243, 173, 0.3));
            animation: rainbowAuraShift 4s ease infinite alternate;
        }

        @keyframes rainbowAuraShift {
            0% { filter: hue-rotate(0deg); }
            100% { filter: hue-rotate(360deg); }
        }

        /* Frost & Star Particles */
        .frost-star-particle {
            position: fixed;
            pointer-events: none;
            z-index: 9999;
            will-change: transform, opacity;
        }
"""

MARKUP_C = """
            <!-- Option C: Interactive Lil Cube Mascot Companion Stage -->
            <div class="companion-stage">
                <div class="companion-dialogue-bubble" id="companionBubble" title="クリックして別の豆知識を聞く！ (Click for next tip!)">
                    <div class="bubble-header">
                        <span>🧊 B-System 氷晶マスコット「リル・キューブ」</span>
                        <span id="tipCounter">TIP 1/8</span>
                    </div>
                    <div class="bubble-content" id="bubbleText">
                        ボクはリル・キューブ！ B-Systemの氷晶プリミティブ・マスコットだキューブ！🧊✨
                    </div>
                    <div class="bubble-hint">💬 クリックで次のシステム豆知識 ➔</div>
                </div>

                <div class="companion-cat-figure" id="companionCatFigure" title="リル・キューブをつつく / なでる (Tap or pet Lil Cube!)">
                    <img src="assets/mascot/lil-cube.png" alt="Lil Cube Mascot" class="companion-cat-img" id="companionCatImg">
                    <div class="companion-cat-pedestal"></div>
                </div>
            </div>
"""

DASHBOARD_C = """
            <div class="companion-dashboard">
                <button id="soundToggleBtn" class="companion-btn" onclick="toggleSound()" style="border-color: #008080; color: #008080;">🔊 8-bit サウンド: ON</button>
                <button id="overdriveBtn" class="companion-btn rainbow-btn" onclick="toggleRainbowOverdrive()">🌈 虹色オーバードライブ</button>
                <button class="companion-btn" onclick="nextMascotTip()">🐾 次のヒント ➔</button>
            </div>
"""

SCRIPT_C = """
    <!-- Script for Option C: Lil Cube Interactive Mascot & Nintendo Web Audio Synthesizer -->
    <script>
    (function() {
        // 1. Nintendo-style 8-Bit Web Audio Synthesizer (Zero External Dependencies)
        let audioCtx = null;
        let soundEnabled = true;

        function getAudioContext() {
            if (!audioCtx) {
                const AudioContext = window.AudioContext || window.webkitAudioContext;
                audioCtx = new AudioContext();
            }
            if (audioCtx.state === 'suspended') {
                audioCtx.resume();
            }
            return audioCtx;
        }

        // Unlock Web Audio context upon first user gesture
        function unlockAudioOnGesture() {
            if (soundEnabled) {
                getAudioContext();
            }
        }
        window.addEventListener('click', unlockAudioOnGesture, { once: true });
        window.addEventListener('keydown', unlockAudioOnGesture, { once: true });
        window.addEventListener('touchstart', unlockAudioOnGesture, { once: true });

        function play8BitNote(freq, type = 'square', duration = 0.12, volume = 0.16) {
            if (!soundEnabled) return;
            try {
                const ctx = getAudioContext();
                const osc = ctx.createOscillator();
                const gain = ctx.createGain();

                osc.type = type;
                osc.frequency.setValueAtTime(freq, ctx.currentTime);

                gain.gain.setValueAtTime(volume, ctx.currentTime);
                gain.gain.exponentialRampToValueAtTime(0.001, ctx.currentTime + duration);

                osc.connect(gain);
                gain.connect(ctx.destination);

                osc.start();
                osc.stop(ctx.currentTime + duration);
            } catch (e) {
                console.error(e);
            }
        }

        // Nintendo Crystal & Coin Chimes
        function sfxCrystalCoin() {
            play8BitNote(1318.51, 'square', 0.08, 0.18); // E6
            setTimeout(() => play8BitNote(1760.00, 'square', 0.24, 0.18), 70); // A6
        }

        function sfxLilCubeBoing() {
            if (!soundEnabled) return;
            const ctx = getAudioContext();
            const osc = ctx.createOscillator();
            const gain = ctx.createGain();
            osc.type = 'triangle';
            osc.frequency.setValueAtTime(523.25, ctx.currentTime);
            osc.frequency.exponentialRampToValueAtTime(1046.50, ctx.currentTime + 0.16);
            osc.frequency.exponentialRampToValueAtTime(783.99, ctx.currentTime + 0.32);
            gain.gain.setValueAtTime(0.22, ctx.currentTime);
            gain.gain.exponentialRampToValueAtTime(0.01, ctx.currentTime + 0.32);
            osc.connect(gain);
            gain.connect(ctx.destination);
            osc.start();
            osc.stop(ctx.currentTime + 0.32);
        }

        function sfxFanfare() {
            const notes = [659.25, 783.99, 987.77, 1318.51];
            notes.forEach((f, idx) => {
                setTimeout(() => play8BitNote(f, 'square', 0.15, 0.2), idx * 80);
            });
        }

        // Sound Toggle Button
        window.toggleSound = function() {
            soundEnabled = !soundEnabled;
            const btn = document.getElementById('soundToggleBtn');
            if (soundEnabled) {
                getAudioContext();
                sfxCrystalCoin();
                btn.innerHTML = '🔊 8-bit サウンド: ON';
                btn.style.borderColor = '#008080';
                btn.style.color = '#008080';
            } else {
                btn.innerHTML = '🔈 8-bit サウンド: OFF';
                btn.style.borderColor = 'rgba(0, 128, 128, 0.35)';
                btn.style.color = '#334155';
            }
        };

        // 2. Lore & Trivia System (Lil Cube Voice)
        const tips = [
            "ボクはリル・キューブ！ B-Systemの氷晶プリミティブ・マスコットだキューブ！🧊✨",
            "実身（Real Body）と仮身（Virtual Body）のハイパーリンク構造でファイル階層から自由だキューブ！",
            "TAD仕様なら、文章・グラフィック・音声が1つのストリームでシームレスに交差するキューブ！",
            "BeOS Media Kitの精神を受け継ぐ極低遅延マルチスレッド！音がカチッと響くキューブ♪",
            "東京大学・坂村健名誉教授のTRON思想を2026年のVirtIO / seL4基盤で再構築したんだキューブ！",
            "超漢字の17万文字多言語コード体系とMozc日本語入力で、どんな文字でもスラスラ打てるキューブ！",
            "ボクをつつくと（クリックすると）ピョコッと跳ねて氷晶の星が舞うキューブ〜⭐",
            "虹色オーバードライブでヘッダーをキラキラのオーロラにするキューブ！🌈✨"
        ];

        let currentTipIdx = 0;
        const bubble = document.getElementById('companionBubble');
        const bubbleText = document.getElementById('bubbleText');
        const tipCounter = document.getElementById('tipCounter');
        const catFigure = document.getElementById('companionCatFigure');

        function nextTip() {
            currentTipIdx = (currentTipIdx + 1) % tips.length;
            bubbleText.style.opacity = '0';
            bubbleText.style.transform = 'translateY(4px)';
            
            sfxCrystalCoin();

            setTimeout(() => {
                bubbleText.innerText = tips[currentTipIdx];
                tipCounter.innerText = `TIP ${currentTipIdx + 1}/${tips.length}`;
                bubbleText.style.opacity = '1';
                bubbleText.style.transform = 'translateY(0)';
            }, 150);
        }

        bubble.addEventListener('click', nextTip);
        window.nextMascotTip = nextTip;

        // Lil Cube Click Interaction
        catFigure.addEventListener('click', () => {
            sfxLilCubeBoing();
            catFigure.style.transform = 'translateY(-24px) scale(1.22) rotate(-8deg)';
            spawnIceStars(catFigure);
            setTimeout(() => {
                catFigure.style.transform = '';
            }, 300);
        });

        // 3. Celebratory Ice Crystal Stars
        function spawnIceStars(element) {
            const rect = element.getBoundingClientRect();
            const cx = rect.left + rect.width / 2;
            const cy = rect.top + rect.height / 2;
            const colors = ['#a2edff', '#ff9fd6', '#fff3ad', '#bcffbc', '#c9b1ff', '#00f5c4'];

            for (let i = 0; i < 24; i++) {
                const star = document.createElement('div');
                star.className = 'frost-star-particle';
                const size = Math.random() * 9 + 6;
                star.style.width = size + 'px';
                star.style.height = size + 'px';
                star.style.backgroundColor = colors[Math.floor(Math.random() * colors.length)];
                star.style.left = cx + 'px';
                star.style.top = cy + 'px';
                star.style.clipPath = 'polygon(50% 0%, 65% 35%, 100% 50%, 65% 65%, 50% 100%, 35% 65%, 0% 50%, 35% 35%)';
                document.body.appendChild(star);

                const angle = Math.random() * Math.PI * 2;
                const dist = Math.random() * 95 + 40;
                const destX = cx + Math.cos(angle) * dist;
                const destY = cy + Math.sin(angle) * dist;

                star.animate([
                    { transform: 'translate(0, 0) scale(1) rotate(0deg)', opacity: 1 },
                    { transform: `translate(${destX - cx}px, ${destY - cy}px) scale(0) rotate(${Math.random() * 360}deg)`, opacity: 0 }
                ], {
                    duration: 650 + Math.random() * 350,
                    easing: 'cubic-bezier(0.25, 1, 0.5, 1)'
                }).onfinish = () => star.remove();
            }
        }

        // 4. Rainbow Overdrive Mode
        let overdriveActive = false;
        window.toggleRainbowOverdrive = function() {
            overdriveActive = !overdriveActive;
            document.body.classList.toggle('rainbow-overdrive', overdriveActive);
            const btn = document.getElementById('overdriveBtn');

            if (overdriveActive) {
                sfxFanfare();
                btn.innerHTML = '✨ 虹色オーバードライブ: ACTIVE!';
                btn.style.filter = 'brightness(1.2)';
                spawnIceStars(btn);
            } else {
                btn.innerHTML = '🌈 虹色オーバードライブ';
                btn.style.filter = '';
            }
        };

        // Auto Cycle Tip Every 10s
        setInterval(() => {
            if (!document.hidden) {
                currentTipIdx = (currentTipIdx + 1) % tips.length;
                bubbleText.innerText = tips[currentTipIdx];
                tipCounter.innerText = `TIP ${currentTipIdx + 1}/${tips.length}`;
            }
        }, 10000);
    })();
    </script>
"""

# ==============================================================================
# OPTION A: HOPF FIBRATION SAINT AURA (WITH LIL CUBE 64)
# ==============================================================================
CSS_A = SHARED_SWITCHER_CSS + """
        /* Option A: Lil Cube 64 Hopf Fibration Saint Aura Styles */
        header {
            position: relative;
            overflow: hidden;
            min-height: 98vh;
            display: flex;
            align-items: center;
            justify-content: center;
            background: radial-gradient(circle at 50% 36%, #161b3d 0%, #0d112b 45%, #050714 85%);
            color: #f8fafc;
        }

        #hopfCanvas {
            position: absolute;
            top: 0;
            left: 0;
            width: 100%;
            height: 100%;
            z-index: 1;
            pointer-events: none;
        }

        .hero-content {
            position: relative;
            z-index: 5;
            width: 100%;
            max-width: 1200px;
            margin: 0 auto;
            padding: 2.2rem 1.5rem;
            display: flex;
            flex-direction: column;
            align-items: center;
        }

        /* Sacred Sanctuary Stage */
        .saint-stage {
            position: relative;
            z-index: 10;
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: center;
            margin: 1.2rem 0 2rem 0;
        }

        .saint-figure-wrapper {
            position: relative;
            width: 320px;
            height: 320px;
            display: flex;
            align-items: center;
            justify-content: center;
            cursor: pointer;
            user-select: none;
        }

        /* Luminous Holy Nimbus (Saint Halo) */
        .saint-nimbus {
            position: absolute;
            width: 290px;
            height: 290px;
            border-radius: 50%;
            background: radial-gradient(circle,
                rgba(255, 245, 180, 0.5) 0%,
                rgba(255, 140, 220, 0.4) 30%,
                rgba(80, 240, 255, 0.3) 55%,
                rgba(160, 100, 255, 0.15) 75%,
                transparent 100%);
            filter: blur(14px);
            animation: saintNimbusGlow 4s ease-in-out infinite alternate;
            pointer-events: none;
        }

        .saint-nimbus-rings {
            position: absolute;
            width: 260px;
            height: 260px;
            border-radius: 50%;
            border: 2px dashed rgba(255, 230, 140, 0.75);
            box-shadow: 0 0 30px rgba(255, 215, 80, 0.5), inset 0 0 25px rgba(80, 240, 255, 0.35);
            animation: saintRingRotate 22s linear infinite;
            pointer-events: none;
        }

        .saint-nimbus-rings::before {
            content: '';
            position: absolute;
            inset: -14px;
            border-radius: 50%;
            border: 1.5px solid rgba(255, 160, 230, 0.55);
            box-shadow: 0 0 16px rgba(255, 120, 220, 0.45);
            animation: saintRingRotateRev 16s linear infinite;
        }

        .saint-nimbus-rings::after {
            content: '';
            position: absolute;
            inset: 14px;
            border-radius: 50%;
            border: 1px dotted rgba(100, 255, 255, 0.8);
        }

        @keyframes saintNimbusGlow {
            0%   { transform: scale(0.94); filter: blur(12px) brightness(1.0); }
            100% { transform: scale(1.10); filter: blur(18px) brightness(1.4); }
        }

        @keyframes saintRingRotate {
            from { transform: rotate(0deg); }
            to   { transform: rotate(360deg); }
        }

        @keyframes saintRingRotateRev {
            from { transform: rotate(360deg); }
            to   { transform: rotate(0deg); }
        }

        /* Levitating Lil Cube 64 Body */
        .saint-cube-body {
            position: relative;
            z-index: 5;
            animation: saintLevitation 4.8s ease-in-out infinite;
            transition: transform 0.3s cubic-bezier(0.34, 1.56, 0.64, 1);
        }

        .saint-cube-img {
            width: 175px;
            height: 175px;
            object-fit: contain;
            display: block;
            filter: drop-shadow(0 12px 30px rgba(0, 255, 255, 0.55)) drop-shadow(0 0 20px rgba(255, 235, 130, 0.65));
            transition: all 0.3s ease;
        }

        .saint-figure-wrapper:hover .saint-cube-body {
            transform: scale(1.08) translateY(-10px);
        }

        .saint-figure-wrapper:active .saint-cube-body {
            transform: scale(0.95) translateY(4px);
        }

        @keyframes saintLevitation {
            0%   { transform: translateY(0px) rotate(0deg); }
            50%  { transform: translateY(-16px) rotate(1.2deg); }
            100% { transform: translateY(0px) rotate(0deg); }
        }

        /* Sacred Dialogue Bubble */
        .saint-speech-bubble {
            background: rgba(14, 18, 44, 0.88);
            border: 2px solid rgba(255, 220, 120, 0.85);
            border-radius: 20px;
            padding: 1.15rem 1.8rem;
            max-width: 620px;
            margin-top: 1.2rem;
            color: #f1f5f9;
            box-shadow: 0 10px 35px rgba(0, 0, 0, 0.55), 0 0 25px rgba(100, 240, 255, 0.28);
            backdrop-filter: blur(16px);
            text-align: center;
            user-select: none;
            cursor: pointer;
            transition: all 0.25s ease;
        }

        .saint-speech-bubble:hover {
            border-color: #ff78d0;
            box-shadow: 0 12px 40px rgba(255, 120, 208, 0.38);
            transform: translateY(-2px);
        }

        .saint-math-badge {
            display: inline-block;
            background: linear-gradient(135deg, rgba(255, 215, 0, 0.22), rgba(0, 255, 255, 0.22));
            border: 1px solid rgba(255, 220, 120, 0.65);
            border-radius: 999px;
            padding: 4px 16px;
            font-size: 0.78rem;
            font-weight: 800;
            letter-spacing: 0.08em;
            color: #ffe066;
            margin-bottom: 0.6rem;
            text-shadow: 0 0 12px rgba(255, 220, 100, 0.65);
        }

        .saint-quote-text {
            font-size: 1.02rem;
            line-height: 1.65;
            color: #ffffff;
            font-weight: 500;
        }

        /* Holiness Dashboard Controls */
        .saint-dashboard {
            display: flex;
            flex-wrap: wrap;
            align-items: center;
            justify-content: center;
            gap: 0.75rem;
            margin-top: 1.3rem;
            z-index: 10;
        }

        .saint-btn {
            background: rgba(18, 24, 58, 0.85);
            border: 1.5px solid rgba(120, 220, 255, 0.5);
            color: #e2e8f0;
            padding: 9px 18px;
            border-radius: 999px;
            font-size: 0.86rem;
            font-weight: 600;
            cursor: pointer;
            box-shadow: 0 4px 15px rgba(0, 0, 0, 0.35);
            backdrop-filter: blur(10px);
            transition: all 0.2s cubic-bezier(0.25, 1, 0.5, 1);
            display: inline-flex;
            align-items: center;
            gap: 6px;
        }

        .saint-btn:hover {
            background: rgba(30, 42, 100, 0.95);
            border-color: #ffd700;
            color: #ffffff;
            transform: translateY(-2px);
            box-shadow: 0 6px 22px rgba(255, 215, 0, 0.4);
        }

        .saint-btn.active {
            background: linear-gradient(135deg, rgba(255, 45, 149, 0.4), rgba(0, 210, 255, 0.4));
            border-color: #00ffff;
            color: #ffffff;
            box-shadow: 0 0 20px rgba(0, 255, 255, 0.55);
        }

        /* Glassmorphic lead box styling for Option A */
        .hero-lead-box {
            background: rgba(14, 18, 42, 0.75) !important;
            border: 1px solid rgba(255, 255, 255, 0.15) !important;
            backdrop-filter: blur(20px) !important;
            box-shadow: 0 16px 40px rgba(0, 0, 0, 0.4) !important;
            color: #e2e8f0 !important;
        }
        .hero-lead-box h2, .hero-lead-box p, .hero-lead-box li {
            color: #f1f5f9 !important;
        }
"""

MARKUP_A = """
        <!-- Option A: Saint Lil Cube 64 with Hopf Fibration Holiness Aura -->
        <div class="saint-stage" id="saintStage">
            <div class="saint-figure-wrapper" id="saintFigure" title="聖なるリル・キューブ64に祈りを捧げる (Click to pray to Saint Lil Cube 64)">
                <div class="saint-nimbus" id="saintNimbus"></div>
                <div class="saint-nimbus-rings"></div>
                <div class="saint-cube-body" id="saintCubeBody">
                    <img src="assets/mascot/lil-cube.png" alt="Saint Lil Cube 64" class="saint-cube-img" id="saintCubeImg">
                </div>
            </div>
            <div class="saint-speech-bubble" id="saintBubble">
                <div class="saint-math-badge">✨ 4次元球面 S³ ホップ束 (Hopf Fibration) 顕現 ✨</div>
                <div class="saint-quote-text" id="saintQuoteText">「ボクはリル・キューブ64… 4次元空間の七色スペクトル光をまとう聖なる氷晶だキューブ！🧊✨」</div>
            </div>
            <div class="saint-dashboard">
                <button id="blessingBtn" class="saint-btn" onclick="triggerBlessing()">🌟 神聖祈願 (Sacred Pulse)</button>
                <button id="dispersionBtn" class="saint-btn active" onclick="toggleDispersion()">🌈 分光モード: ON</button>
                <button id="soundToggleBtn" class="saint-btn" onclick="toggleSound()" style="border-color: #00ffff; color: #00ffff;">🔊 聖なる天球音楽: ON</button>
                <button id="densityBtn" class="saint-btn" onclick="cycleDensity()">🪐 繊維環: 36環</button>
                <button id="pixelToggleBtn" class="saint-btn" onclick="togglePixelArt()">🧊 64×64 Pixel Art</button>
            </div>
        </div>
"""

SCRIPT_A = """
    <!-- Script for Option A: Hopf Fibration 4D Projection, Spectral Dispersion & Sacred Audio -->
    <script>
    (function() {
        const canvas = document.getElementById('hopfCanvas');
        if (!canvas) return;
        const ctx = canvas.getContext('2d');
        const figure = document.getElementById('saintFigure');
        const cubeBody = document.getElementById('saintCubeBody');
        const cubeImg = document.getElementById('saintCubeImg');
        const quoteText = document.getElementById('saintQuoteText');
        const bubble = document.getElementById('saintBubble');

        let width = canvas.width = canvas.parentElement.offsetWidth;
        let height = canvas.height = canvas.parentElement.offsetHeight;

        window.addEventListener('resize', () => {
            if (!canvas.parentElement) return;
            width = canvas.width = canvas.parentElement.offsetWidth;
            height = canvas.height = canvas.parentElement.offsetHeight;
        });

        // 1. Web Audio Synthesizer: Pythagorean Harmony of the Spheres (Celestial Bells)
        let audioCtx = null;
        window.soundEnabled = true;

        function getAudioContext() {
            if (!audioCtx) {
                const AudioContext = window.AudioContext || window.webkitAudioContext;
                audioCtx = new AudioContext();
            }
            if (audioCtx.state === 'suspended') {
                audioCtx.resume();
            }
            return audioCtx;
        }

        function unlockAudio() {
            if (window.soundEnabled) getAudioContext();
        }
        window.addEventListener('click', unlockAudio, { once: true });
        window.addEventListener('keydown', unlockAudio, { once: true });
        window.addEventListener('touchstart', unlockAudio, { once: true });

        function playCelestialChime(freq, duration = 0.8, vol = 0.16) {
            if (!window.soundEnabled) return;
            try {
                const c = getAudioContext();
                const osc = c.createOscillator();
                const gain = c.createGain();

                osc.type = 'sine';
                osc.frequency.setValueAtTime(freq, c.currentTime);

                gain.gain.setValueAtTime(vol, c.currentTime);
                gain.gain.exponentialRampToValueAtTime(0.0001, c.currentTime + duration);

                osc.connect(gain);
                gain.connect(c.destination);

                osc.start();
                osc.stop(c.currentTime + duration);
            } catch (e) {
                console.error(e);
            }
        }

        function playSacredChord() {
            // D major pentatonic sacred harmonic series
            const freqs = [587.33, 659.25, 739.99, 880.00, 1174.66, 1318.51];
            freqs.forEach((f, idx) => {
                setTimeout(() => playCelestialChime(f, 1.4 + idx * 0.15, 0.12), idx * 65);
            });
        }

        window.toggleSound = function() {
            window.soundEnabled = !window.soundEnabled;
            const btn = document.getElementById('soundToggleBtn');
            if (window.soundEnabled) {
                getAudioContext();
                playSacredChord();
                btn.innerHTML = '🔊 聖なる天球音楽: ON';
                btn.style.borderColor = '#00ffff';
                btn.style.color = '#00ffff';
            } else {
                btn.innerHTML = '🔈 聖なる天球音楽: OFF';
                btn.style.borderColor = 'rgba(120, 220, 255, 0.5)';
                btn.style.color = '#94a3b8';
            }
        };

        // 2. Sacred Holy Particles (Sparkles & Stars)
        const holyParticles = [];
        class HolySparkle {
            constructor(x, y, isBurst = false) {
                this.x = x;
                this.y = y;
                this.size = Math.random() * (isBurst ? 14 : 7) + 3;
                const speed = isBurst ? 5.5 : 1.2;
                const angle = Math.random() * Math.PI * 2;
                this.vx = Math.cos(angle) * speed * (Math.random() + 0.4);
                this.vy = Math.sin(angle) * speed * (Math.random() + 0.4) - (isBurst ? 1.0 : 1.2);
                this.alpha = 1.0;
                this.decay = Math.random() * 0.015 + (isBurst ? 0.018 : 0.008);
                this.hue = (Math.random() * 360);
                this.rot = Math.random() * Math.PI * 2;
                this.rotSpeed = (Math.random() - 0.5) * 0.1;
                this.isCross = Math.random() > 0.45;
            }
            update() {
                this.x += this.vx;
                this.y += this.vy;
                this.alpha -= this.decay;
                this.rot += this.rotSpeed;
            }
            draw(ctx) {
                if (this.alpha <= 0) return;
                ctx.save();
                ctx.globalAlpha = Math.max(0, this.alpha);
                ctx.translate(this.x, this.y);
                ctx.rotate(this.rot);
                ctx.fillStyle = `hsla(${this.hue}, 95%, 75%, 1)`;
                ctx.shadowColor = `hsla(${this.hue}, 100%, 70%, 0.8)`;
                ctx.shadowBlur = 10;

                const r = this.size;
                ctx.beginPath();
                if (this.isCross) {
                    ctx.moveTo(0, -r);
                    ctx.lineTo(r * 0.25, -r * 0.25);
                    ctx.lineTo(r, 0);
                    ctx.lineTo(r * 0.25, r * 0.25);
                    ctx.lineTo(0, r);
                    ctx.lineTo(-r * 0.25, r * 0.25);
                    ctx.lineTo(-r, 0);
                    ctx.lineTo(-r * 0.25, -r * 0.25);
                } else {
                    ctx.arc(0, 0, r * 0.4, 0, Math.PI * 2);
                }
                ctx.closePath();
                ctx.fill();
                ctx.restore();
            }
        }

        // 3. Mathematical Hopf Fibration 4D Engine
        // S^1 -> S^3 -> S^2
        let time = 0;
        let mouseX = 0;
        let mouseY = 0;
        let targetRotX = 0.2;
        let targetRotY = 0;
        let curRotX = 0.2;
        let curRotY = 0;

        let dispersionActive = true;
        let densityMode = 1; // 0: 18, 1: 36, 2: 54
        const densityConfigs = [
            { tori: 2, fibersPerTorus: 9, pts: 40, label: "18環" },
            { tori: 3, fibersPerTorus: 12, pts: 46, label: "36環" },
            { tori: 3, fibersPerTorus: 18, pts: 52, label: "54環" }
        ];

        window.addEventListener('mousemove', (e) => {
            const rect = canvas.getBoundingClientRect();
            if (e.clientY >= rect.top && e.clientY <= rect.bottom) {
                const normX = (e.clientX - rect.left) / width - 0.5;
                const normY = (e.clientY - rect.top) / height - 0.5;
                targetRotY = normX * 1.2;
                targetRotX = normY * 0.8 + 0.25;
            }
        });

        window.toggleDispersion = function() {
            dispersionActive = !dispersionActive;
            const btn = document.getElementById('dispersionBtn');
            if (dispersionActive) {
                btn.classList.add('active');
                btn.innerHTML = '🌈 分光モード: ON';
            } else {
                btn.classList.remove('active');
                btn.innerHTML = '✨ 単色光モード: OFF';
            }
        };

        window.cycleDensity = function() {
            densityMode = (densityMode + 1) % densityConfigs.length;
            const btn = document.getElementById('densityBtn');
            btn.innerHTML = `🪐 繊維環: ${densityConfigs[densityMode].label}`;
            playCelestialChime(880.00, 0.4, 0.15);
        };

        let isPixelArt = false;
        window.togglePixelArt = function() {
            isPixelArt = !isPixelArt;
            const btn = document.getElementById('pixelToggleBtn');
            if (isPixelArt) {
                cubeImg.src = 'assets/pixart/lil-cube-64.png';
                cubeImg.style.imageRendering = 'pixelated';
                btn.innerHTML = '🧊 HD Crystal Art';
            } else {
                cubeImg.src = 'assets/mascot/lil-cube.png';
                cubeImg.style.imageRendering = 'auto';
                btn.innerHTML = '🧊 64×64 Pixel Art';
            }
            playCelestialChime(1046.50, 0.5, 0.16);
        };

        let blessingActive = false;
        window.triggerBlessing = function() {
            if (blessingActive) return;
            blessingActive = true;
            playSacredChord();

            // Spawn radiant star shower
            const rect = figure.getBoundingClientRect();
            const canvasRect = canvas.getBoundingClientRect();
            const cx = rect.left + rect.width / 2 - canvasRect.left;
            const cy = rect.top + rect.height / 2 - canvasRect.top;

            for (let i = 0; i < 48; i++) {
                holyParticles.push(new HolySparkle(cx, cy, true));
            }

            figure.style.transform = 'scale(1.18) rotate(3deg)';
            setTimeout(() => {
                figure.style.transform = '';
                blessingActive = false;
            }, 600);

            nextSacredQuote();
        };

        figure.addEventListener('click', triggerBlessing);

        const sacredQuotes = [
            "「ボクはリル・キューブ64… 4次元球面 S³ のホップ束より顕現せし聖なる氷晶だキューブ！🧊✨」",
            "「すべての繊維環（Fiber Circle）は互いに絡み合い、聖なる加護をもたらすキューブ！🌈🙏」",
            "「実身と仮身、ハイパーメディアの永遠の連鎖… TRONの調和がここに極まるキューブ！✨」",
            "「キミの心に七色の分光を！ 氷晶の聖なる輝きで満たされるキューブ〜⭐🔔」",
            "「4次元回転の波長が重なり合い、天球の音楽が鳴り響いているキューブ♪」",
            "「ホップ不変量（Hopf Invariant）は常に1！ この絆は絶対に解けないキューブ！🧊💫」"
        ];
        let quoteIdx = 0;
        function nextSacredQuote() {
            quoteIdx = (quoteIdx + 1) % sacredQuotes.length;
            quoteText.innerText = sacredQuotes[quoteIdx];
        }
        bubble.addEventListener('click', () => {
            nextSacredQuote();
            playCelestialChime(739.99, 0.5, 0.15);
        });

        // 4. Main 60 FPS Render Loop
        function animate() {
            time += 0.018;
            curRotX += (targetRotX - curRotX) * 0.05;
            curRotY += (targetRotY - curRotY) * 0.05;

            ctx.clearRect(0, 0, width, height);

            // Compute center of Saint Lil Cube
            let cx = width * 0.5;
            let cy = height * 0.38;
            if (figure && canvas.parentElement) {
                const rect = figure.getBoundingClientRect();
                const parentRect = canvas.parentElement.getBoundingClientRect();
                cx = (rect.left + rect.width * 0.5) - parentRect.left;
                cy = (rect.top + rect.height * 0.5) - parentRect.top;
            }

            const cfg = densityConfigs[densityMode];
            const numTori = cfg.tori;
            const fibersPerTorus = cfg.fibersPerTorus;
            const numPts = cfg.pts;
            const totalFibers = numTori * fibersPerTorus;

            // Base scale for stereographic projection
            const hopfScale = Math.min(width, height) * 0.28;

            // Rotation matrices for 3D view
            const cosX = Math.cos(curRotX), sinX = Math.sin(curRotX);
            const cosY = Math.cos(curRotY + time * 0.18), sinY = Math.sin(curRotY + time * 0.18);
            const cosZ = Math.cos(time * 0.12), sinZ = Math.sin(time * 0.12);

            // 4D isoclinic rotation phases
            const psi = time * 0.42;  // Fiber circle rotation
            const omega = time * 0.22; // Base space precession

            // Precompute fiber curves
            const fiberList = [];

            for (let t = 0; t < numTori; t++) {
                // eta parameter determines torus radius in projection
                const eta = (Math.PI / 7) + (t + 0.6) * (Math.PI / (numTori * 2.8));
                const cosEta = Math.cos(eta);
                const sinEta = Math.sin(eta);

                for (let f = 0; f < fibersPerTorus; f++) {
                    const fiberIdx = t * fibersPerTorus + f;
                    const beta = (f * 2 * Math.PI / fibersPerTorus) + omega;

                    const pts2d = [];
                    let avgZ = 0;

                    for (let p = 0; p <= numPts; p++) {
                        const xi = (p * 2 * Math.PI / numPts);

                        // 4D coordinates on S^3
                        const x0 = cosEta * Math.cos(xi + beta * 0.5 + psi);
                        const x1 = cosEta * Math.sin(xi + beta * 0.5 + psi);
                        const x2 = sinEta * Math.cos(xi - beta * 0.5 + psi * 0.8);
                        const x3 = sinEta * Math.sin(xi - beta * 0.5 + psi * 0.8);

                        // Stereographic projection S^3 -> R^3 from (0,0,0,1)
                        const denom = 1.08 - x3;
                        const X3d = x0 / denom;
                        const Y3d = x1 / denom;
                        const Z3d = x2 / denom;

                        // 3D rotation (Yaw * Pitch * Roll)
                        // 1. Yaw (Y)
                        const x_yaw = X3d * cosY + Z3d * sinY;
                        const y_yaw = Y3d;
                        const z_yaw = -X3d * sinY + Z3d * cosY;

                        // 2. Pitch (X)
                        const x_pitch = x_yaw;
                        const y_pitch = y_yaw * cosX - z_yaw * sinX;
                        const z_pitch = y_yaw * sinX + z_yaw * cosX;

                        // 3. Roll (Z)
                        const x_rot = x_pitch * cosZ - y_pitch * sinZ;
                        const y_rot = x_pitch * sinZ + y_pitch * cosZ;
                        const z_rot = z_pitch;

                        // Camera perspective
                        const camDist = 3.6;
                        const pers = camDist / (camDist + z_rot);
                        const scrX = cx + x_rot * hopfScale * pers;
                        const scrY = cy + y_rot * hopfScale * pers;

                        pts2d.push({ x: scrX, y: scrY, z: z_rot });
                        avgZ += z_rot;
                    }

                    avgZ /= (numPts + 1);

                    // Spectral color mapping
                    let hue;
                    if (dispersionActive) {
                        hue = ((fiberIdx / totalFibers) * 360 + time * 32) % 360;
                    } else {
                        hue = 180 + Math.sin(time + fiberIdx) * 30; // Divine crystalline cyan
                    }

                    fiberList.push({
                        pts: pts2d,
                        avgZ: avgZ,
                        hue: hue,
                        torusIdx: t
                    });
                }
            }

            // Depth sorting (furthest first)
            fiberList.sort((a, b) => a.avgZ - b.avgZ);

            // Draw Hopf Fibers with glowing spectral gradient
            ctx.save();
            ctx.lineCap = 'round';
            ctx.lineJoin = 'round';

            for (let f = 0; f < fiberList.length; f++) {
                const fib = fiberList[f];
                const pts = fib.pts;

                // Opacity & line width depend on depth
                const depthAlpha = Math.max(0.25, Math.min(1.0, 0.65 + fib.avgZ * 0.35));
                const lineWidth = Math.max(1.8, (2.8 + fib.avgZ * 1.2));

                ctx.beginPath();
                ctx.moveTo(pts[0].x, pts[0].y);
                for (let i = 1; i < pts.length; i++) {
                    ctx.lineTo(pts[i].x, pts[i].y);
                }

                ctx.strokeStyle = `hsla(${fib.hue}, 95%, 68%, ${depthAlpha})`;
                ctx.lineWidth = lineWidth;
                ctx.shadowColor = `hsla(${fib.hue}, 100%, 72%, 0.8)`;
                ctx.shadowBlur = dispersionActive ? 14 : 8;

                if (fib.avgZ > 0.1 && dispersionActive) {
                    ctx.globalCompositeOperation = 'lighter';
                } else {
                    ctx.globalCompositeOperation = 'source-over';
                }

                ctx.stroke();

                // Sparkle at caustic highlight nodes
                if (Math.random() < 0.04) {
                    const midPt = pts[Math.floor(pts.length / 2)];
                    if (midPt && holyParticles.length < 65) {
                        holyParticles.push(new HolySparkle(midPt.x, midPt.y));
                    }
                }
            }
            ctx.restore();

            // Background holy particle emitters
            if (Math.random() < 0.22 && holyParticles.length < 65) {
                const rx = cx + (Math.random() - 0.5) * 220;
                const ry = cy + (Math.random() - 0.5) * 180;
                holyParticles.push(new HolySparkle(rx, ry));
            }

            // Draw Holy Particles
            for (let i = holyParticles.length - 1; i >= 0; i--) {
                const p = holyParticles[i];
                p.update();
                p.draw(ctx);
                if (p.alpha <= 0) {
                    holyParticles.splice(i, 1);
                }
            }

            requestAnimationFrame(animate);
        }

        animate();
    })();
    </script>
"""

# ==============================================================================
# OPTION B: COSMIC FLIGHT (WITH LIL CUBE)
# ==============================================================================
CSS_B = SHARED_SWITCHER_CSS + """
        /* Option B: Cosmic Flight Styles */
        header {
            position: relative;
            overflow: hidden;
            background: linear-gradient(180deg, #fff0f8 0%, #ffe6f3 50%, #f0e8ff 100%);
            min-height: 94vh;
            display: flex;
            align-items: center;
        }

        #cosmicFlightCanvas {
            position: absolute;
            top: 0;
            left: 0;
            width: 100%;
            height: 100%;
            z-index: 1;
        }

        .hero-content {
            position: relative;
            z-index: 5;
        }

        .flight-control-bar {
            display: inline-flex;
            align-items: center;
            gap: 12px;
            background: rgba(255, 255, 255, 0.88);
            backdrop-filter: blur(14px);
            border: 1.5px solid rgba(0, 128, 128, 0.4);
            border-radius: 999px;
            padding: 6px 18px;
            margin-bottom: 1rem;
            box-shadow: 0 8px 24px rgba(0, 128, 128, 0.18);
        }

        .turbo-toggle-btn {
            background: linear-gradient(135deg, #008080, #0057b7);
            color: #ffffff;
            border: none;
            border-radius: 999px;
            padding: 5px 14px;
            font-size: 0.82rem;
            font-weight: 700;
            cursor: pointer;
            display: inline-flex;
            align-items: center;
            gap: 6px;
            box-shadow: 0 4px 12px rgba(0, 128, 128, 0.35);
            transition: all 0.25s cubic-bezier(0.34, 1.56, 0.64, 1);
        }

        .turbo-toggle-btn:hover {
            transform: scale(1.08);
            box-shadow: 0 6px 18px rgba(0, 128, 128, 0.5);
        }

        .turbo-toggle-btn.active {
            background: linear-gradient(135deg, #ff2d95, #ff9900);
            box-shadow: 0 0 20px rgba(255, 153, 0, 0.6);
            animation: turboPulsing 1s infinite alternate;
        }

        @keyframes turboPulsing {
            0% { transform: scale(1); filter: brightness(1); }
            100% { transform: scale(1.06); filter: brightness(1.2); }
        }

        .flight-speed-indicator {
            font-size: 0.82rem;
            font-weight: 700;
            color: #475569;
            font-family: 'JetBrains Mono', monospace;
        }
"""

MARKUP_B = """
        <!-- Option B: Full Hero Cosmic Flight Canvas -->
        <canvas id="cosmicFlightCanvas"></canvas>
"""

SCRIPT_B = """
    <!-- Script for Option B: Lil Cube Cosmic Flight & Rainbow Ice Ribbon -->
    <script>
    (function() {
        const canvas = document.getElementById('cosmicFlightCanvas');
        if (!canvas) return;
        const ctx = canvas.getContext('2d');
        const header = canvas.parentElement;

        let width = canvas.width = header.offsetWidth;
        let height = canvas.height = header.offsetHeight;

        window.addEventListener('resize', () => {
            width = canvas.width = header.offsetWidth;
            height = canvas.height = header.offsetHeight;
        });

        // Load Lil Cube Image
        const cubeImg = new Image();
        cubeImg.src = 'assets/mascot/lil-cube.png';

        const rainbowColors = [
            '#ff3399', '#ff9900', '#ffff00', '#33ff00', '#0099ff', '#6633ff'
        ];

        const stars = [];
        const starCount = 85;
        for (let i = 0; i < starCount; i++) {
            stars.push({
                x: Math.random() * width,
                y: Math.random() * height,
                size: Math.random() * 3 + 1,
                alpha: Math.random() * 0.8 + 0.2,
                twinkleSpeed: Math.random() * 0.04 + 0.015,
                isDiamond: Math.random() < 0.25,
                color: ['#ffffff', '#a2edff', '#ffb6e0', '#fff4b8'][Math.floor(Math.random() * 4)]
            });
        }

        let flightX = -100;
        let flightY = height * 0.28;
        let baseSpeed = 3.6;
        let currentSpeed = baseSpeed;
        let isTurbo = false;
        let time = 0;

        const trailPoints = [];
        const maxTrailLength = 400;

        let mouseX = width / 2;
        let mouseY = height / 2;
        let targetParallaxX = 0;
        let currentParallaxX = 0;

        window.addEventListener('mousemove', (e) => {
            const rect = canvas.getBoundingClientRect();
            if (e.clientY >= rect.top && e.clientY <= rect.bottom) {
                mouseX = e.clientX - rect.left;
                mouseY = e.clientY - rect.top;
                targetParallaxX = (mouseX - width / 2) * 0.035;
            }
        });

        canvas.addEventListener('click', (e) => {
            const dist = Math.hypot(e.clientX - canvas.getBoundingClientRect().left - flightX, e.clientY - canvas.getBoundingClientRect().top - flightY);
            if (dist < 120) {
                toggleTurbo();
            }
        });

        function toggleTurbo() {
            isTurbo = !isTurbo;
            const btn = document.getElementById('turboBtn');
            const speedText = document.getElementById('speedText');
            if (btn) btn.classList.toggle('active', isTurbo);
            if (speedText) speedText.innerText = isTurbo ? 'WARP SPEED 9.8 ⚡' : 'CRUISE 3.6 🧊';
        }
        window.toggleTurbo = toggleTurbo;

        function animate() {
            time += 0.03;
            currentParallaxX += (targetParallaxX - currentParallaxX) * 0.05;

            const targetSpeed = isTurbo ? 10.5 : baseSpeed;
            currentSpeed += (targetSpeed - currentSpeed) * 0.12;

            flightX += currentSpeed;
            if (flightX > width + 220) {
                flightX = -180;
                trailPoints.length = 0;
            }

            flightY = height * 0.24 + Math.sin(flightX * 0.012 + time * 1.5) * 32;

            trailPoints.unshift({
                x: flightX - 35,
                y: flightY,
                time: time
            });

            if (trailPoints.length > maxTrailLength) {
                trailPoints.pop();
            }

            ctx.clearRect(0, 0, width, height);

            for (let star of stars) {
                star.alpha += Math.sin(time * 60 * star.twinkleSpeed) * 0.02;
                star.alpha = Math.max(0.15, Math.min(1.0, star.alpha));

                ctx.save();
                ctx.globalAlpha = star.alpha;
                ctx.fillStyle = star.color;

                const sx = (star.x + currentParallaxX + width) % width;
                const sy = star.y;

                if (isTurbo) {
                    ctx.strokeStyle = star.color;
                    ctx.lineWidth = star.size * 1.2;
                    ctx.beginPath();
                    ctx.moveTo(sx, sy);
                    ctx.lineTo(sx - currentSpeed * 6, sy);
                    ctx.stroke();
                } else if (star.isDiamond) {
                    ctx.translate(sx, sy);
                    const s = star.size * 2.2;
                    ctx.beginPath();
                    ctx.moveTo(0, -s);
                    ctx.lineTo(s * 0.35, 0);
                    ctx.lineTo(0, s);
                    ctx.lineTo(-s * 0.35, 0);
                    ctx.closePath();
                    ctx.fill();
                } else {
                    ctx.beginPath();
                    ctx.arc(sx, sy, star.size, 0, Math.PI * 2);
                    ctx.fill();
                }
                ctx.restore();
            }

            if (trailPoints.length > 2) {
                const stripeHeight = 6.5;
                const totalHeight = stripeHeight * 6;

                for (let c = 0; c < rainbowColors.length; c++) {
                    ctx.save();
                    ctx.fillStyle = rainbowColors[c];
                    ctx.shadowColor = rainbowColors[c];
                    ctx.shadowBlur = isTurbo ? 16 : 6;

                    for (let i = 0; i < trailPoints.length - 1; i++) {
                        const pt = trailPoints[i];
                        const nextPt = trailPoints[i + 1];

                        const stepVibration = Math.round(Math.sin((pt.x * 0.08) + pt.time * 4)) * 3;
                        const topY = (pt.y - totalHeight / 2) + (c * stripeHeight) + stepVibration;
                        const segW = Math.max(4, Math.abs(pt.x - nextPt.x) + 1.5);

                        const fade = 1 - (i / trailPoints.length);
                        ctx.globalAlpha = Math.max(0, fade * 0.95);

                        ctx.fillRect(pt.x - segW, topY, segW, stripeHeight);
                    }
                    ctx.restore();
                }
            }

            if (cubeImg.complete) {
                ctx.save();
                const cubeSize = 135;
                const bob = Math.round(Math.sin(time * 10)) * 4;

                ctx.translate(flightX, flightY + bob);

                if (isTurbo) {
                    ctx.scale(1.15, 0.88);
                    ctx.shadowColor = '#008080';
                    ctx.shadowBlur = 24;
                } else {
                    ctx.shadowColor = 'rgba(0, 128, 128, 0.4)';
                    ctx.shadowBlur = 12;
                }

                ctx.drawImage(cubeImg, -cubeSize / 2, -cubeSize / 2, cubeSize, cubeSize);
                ctx.restore();
            }

            requestAnimationFrame(animate);
        }

        animate();
    })();
    </script>
"""

def generate_page(variant_key, css_block, markup_block, script_block, output_filename, is_option_c=False):
    html = clean_html
    switcher_html = get_switcher_bar(variant_key)
    html = html.replace("<body>", f"<body>\n{switcher_html}")

    style_idx = html.find("</style>")
    if style_idx != -1:
        html = html[:style_idx] + css_block + "\n    " + html[style_idx:]

    if is_option_c:
        target_pos = html.find('<div class="hero-lead-box">')
        if target_pos != -1:
            html = html[:target_pos] + markup_block + "\n            " + html[target_pos:]
        
        dash_pos = html.find('<div style="margin-top: 1.8rem;')
        if dash_pos != -1:
            html = html[:dash_pos] + DASHBOARD_C + "\n            " + html[dash_pos:]
    elif variant_key == 'a':
        header_idx = html.find("<header>")
        if header_idx != -1:
            html = html[:header_idx + 8] + '\n        <canvas id="hopfCanvas"></canvas>' + html[header_idx + 8:]
        target_pos = html.find('<div class="hero-lead-box">')
        if target_pos != -1:
            html = html[:target_pos] + markup_block + "\n            " + html[target_pos:]
    else:
        header_content_idx = html.find('<div class="hero-content">')
        if header_content_idx != -1:
            if variant_key == 'b':
                turbo_bar = """
            <div style="text-align: center; margin-bottom: 0.6rem;">
                <div class="flight-control-bar">
                    <button id="turboBtn" class="turbo-toggle-btn" onclick="toggleTurbo()">🚀 ターボモード (WARP SPEED)</button>
                    <span id="speedText" class="flight-speed-indicator">CRUISE 3.6 🧊</span>
                </div>
            </div>
"""
                html = html[:header_content_idx] + markup_block + "\n        " + html[header_content_idx:]
                tag_idx = html.find('<div class="hero-banner-tag"')
                if tag_idx != -1:
                    html = html[:tag_idx] + turbo_bar + "\n            " + html[tag_idx:]
            else:
                html = html[:header_content_idx] + markup_block + "\n        " + html[header_content_idx:]

    body_close_idx = html.rfind("</body>")
    if body_close_idx != -1:
        html = html[:body_close_idx] + script_block + "\n" + html[body_close_idx:]

    out_path = os.path.join(ROOT_DIR, output_filename)
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(html)
    print(f"Generated {output_filename} successfully ({len(html)} bytes).")

# 1. Generate Option A
generate_page('a', CSS_A, MARKUP_A, SCRIPT_A, 'index-a.html')

# 2. Generate Option B
generate_page('b', CSS_B, MARKUP_B, SCRIPT_B, 'index-b.html')

# 3. Generate Option C
generate_page('c', CSS_C, MARKUP_C, SCRIPT_C, 'index-c.html', is_option_c=True)

# 4. Also update main index.html to deploy Option C as the primary official experience!
generate_page('main', CSS_C, MARKUP_C, SCRIPT_C, 'index.html', is_option_c=True)

print("All pages generated and updated with Lil Cube mascot!")
