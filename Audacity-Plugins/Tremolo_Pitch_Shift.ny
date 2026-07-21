;nyquist plug-in
;version 4
;type process
;name "Tremolo Pitch Shift..."
;action "Applying Tremolo Pitch Shift..."
;author "jwesters / OpenAI [vibe coded]"
;release 1.5.0
;copyright "Released under the terms of the GNU General Public License version 2 or later."
;preview enabled
;debugbutton enabled

;control text "TREMOLO"
;control trem-rate "Rate" float "Hz" 5.0 0.1 20.0
;control trem-depth "Depth" float "%" 80.0 0.0 100.0
;control waveform "Waveform" choice "Sine,Triangle,Square,Saw Up,Saw Down" 0
;control waveform-smoothing "Waveform edge smoothing" float "ms" 5.0 0.0 20.0
;control pitch-crossfade "Pitch-change crossfade" float "ms" 8.0 0.0 25.0
;control output-headroom "Output headroom" float "dB" 0.0 0.0 6.0

;control text "PITCH - each complete tremolo cycle counts as one pulse"
;control pitch-mode "Pitch behaviour" choice "Progressive,Reset after pulses,Alternate between two pitches,Rise and fall,Stop at maximum,Random within range" 0
;control pitch-direction "Progressive direction" choice "Higher,Lower" 0
;control pitch-step "Pitch change per pulse" float "semitones" 1.0 0.0 12.0
;control safety-limit "Maximum absolute pitch" float "semitones" 24.0 1.0 24.0

;control text "MODE-SPECIFIC CONTROLS - unused settings are ignored"
;control reset-pulses "Reset after" int "pulses" 8 2 64
;control alternate-a "Alternate pitch A" float "semitones" 0.0 -24.0 24.0
;control alternate-b "Alternate pitch B" float "semitones" 7.0 -24.0 24.0
;control rise-limit "Rise-and-fall peak" float "semitones" 12.0 1.0 24.0
;control stop-limit "Stop-at-maximum limit" float "semitones" 12.0 1.0 24.0
;control random-min "Random minimum" float "semitones" -12.0 -24.0 24.0
;control random-max "Random maximum" float "semitones" 12.0 -24.0 24.0
;control random-seed "Random seed" int "" 12345 1 999999

;; Tremolo Pitch Shift 1.5.0
;;
;; Anti-static / anti-click changes:
;;   - Adjacent pitch-shifted pulse regions overlap and crossfade rather than
;;     meeting at a hard edit point.
;;   - Square and saw tremolo control signals use SND-CHASE to soften their
;;     discontinuous edges.
;;   - Adjustable output headroom remains available, but defaults to 0 dB.
;;   - Equal-power tremolo reduces unnecessary perceived-volume loss.
;;
;; The output always has the same duration as the selected audio.

(setf +pi+ 3.141592653589793)
(setf +maximum-pulses+ 400)
(setf +maximum-duration+ 300.0)

(defun clamp-number (number minimum maximum)
  (max minimum (min maximum number)))

(defun ceil-positive (number)
  (let ((whole (truncate number)))
    (if (> number whole)
        (+ whole 1)
        whole)))

(defun directed-pitch (magnitude)
  (if (= pitch-direction 0)
      magnitude
      (- magnitude)))

(defun safe-pitch (semitones)
  (clamp-number semitones (- safety-limit) safety-limit))

(defun semitones-to-ratio (semitones)
  (power 2.0 (/ semitones 12.0)))

(defun pseudo-random-unit (index)
  ;; Deterministic pseudo-random value so Preview and Apply match.
  (let* ((angle (+ (* (+ random-seed 1) 12.9898)
                   (* (+ index 1) 78.233)))
         (number (abs (* 43758.5453 (sin angle))))
         (whole (truncate number)))
    (- number whole)))

(defun pitch-for-pulse (index)
  (safe-pitch
    (cond
      ((= pitch-mode 0)
       (directed-pitch (* index pitch-step)))

      ((= pitch-mode 1)
       (directed-pitch (* (rem index reset-pulses) pitch-step)))

      ((= pitch-mode 2)
       (if (= (rem index 2) 0)
           alternate-a
           alternate-b))

      ((= pitch-mode 3)
       (if (<= pitch-step 0.0)
           0.0
           (let* ((peak (min rise-limit safety-limit))
                  (steps (max 1 (ceil-positive (/ peak pitch-step))))
                  (cycle-length (* 2 steps))
                  (position (rem index cycle-length))
                  (triangle-position
                    (if (> position steps)
                        (- cycle-length position)
                        position)))
             (directed-pitch
               (min peak (* triangle-position pitch-step))))))

      ((= pitch-mode 4)
       (directed-pitch
         (min (min stop-limit safety-limit)
              (* index pitch-step))))

      (t
       (+ random-min
          (* (- random-max random-min)
             (pseudo-random-unit index)))))))

(defun reset-to-zero (sig)
  (at-abs 0 (cue sig)))

(defun get-pulse (sig start stop)
  ;; EXTRACT-ABS returns the requested region at time zero. Resetting again is
  ;; harmless and keeps the behaviour consistent across Audacity versions.
  (reset-to-zero
    (extract-abs start stop (cue sig))))

(defun shift-pulse (pulse pulse-duration semitones)
  (let ((shifted
          (if (< (abs semitones) 0.000001)
              pulse
              (pitshift pulse (semitones-to-ratio semitones) 1.0))))
    ;; PITSHIFT may emit a tiny tail. Keep the exact requested duration.
    (extract-abs 0 pulse-duration (cue shifted))))

(defun constant-envelope (duration)
  (stretch-abs 1.0
    (pwlv 1.0 duration 1.0)))

(defun pulse-crossfade-envelope
       (index pulse-count nominal-duration segment-duration crossfade)
  ;; Adjacent linear fades are complementary. At a pitch boundary, the old
  ;; pulse fades from 1 to 0 while the new pulse fades from 0 to 1 over the
  ;; same interval, avoiding a hard waveform jump.
  (cond
    ((or (<= crossfade 0.0) (= pulse-count 1))
     (constant-envelope segment-duration))

    ;; First pulse: no fade-in, only a fade-out around its right boundary.
    ((= index 0)
     (stretch-abs 1.0
       (pwlv 1.0
             (max 0.0 (- nominal-duration crossfade)) 1.0
             segment-duration 0.0)))

    ;; Last pulse: fade in around its left boundary, no fade-out at selection end.
    ((= index (- pulse-count 1))
     (stretch-abs 1.0
       (pwlv 0.0
             (* 2.0 crossfade) 1.0
             segment-duration 1.0)))

    ;; Middle pulse: fade in at the left boundary and out at the right boundary.
    (t
     (stretch-abs 1.0
       (pwlv 0.0
             (* 2.0 crossfade) 1.0
             nominal-duration 1.0
             segment-duration 0.0)))))

(defun effective-crossfade (duration period pulse-count)
  ;; Never allow the overlap to consume most of a pulse. The final pulse can
  ;; be shorter than one full period, so clamp against the shortest pulse.
  (let* ((last-duration
           (- duration (* (- pulse-count 1) period)))
         (shortest-duration
           (if (= pulse-count 1)
               duration
               (min period last-duration))))
    (max 0.0
      (min (/ pitch-crossfade 1000.0)
           (* shortest-duration 0.20)))))

(defun assemble-pitched-pulses (sig duration period pulse-count)
  (let ((output (s-rest 0))
        (crossfade (effective-crossfade duration period pulse-count)))
    (do ((index 0 (1+ index)))
        ((>= index pulse-count)
         (extract-abs 0 duration (cue output)))
      (let* ((nominal-start (* index period))
             (nominal-stop
               (if (= index (- pulse-count 1))
                   duration
                   (min duration (+ nominal-start period))))
             (nominal-duration (- nominal-stop nominal-start))
             (left-overlap (if (= index 0) 0.0 crossfade))
             (right-overlap
               (if (= index (- pulse-count 1)) 0.0 crossfade))
             (segment-start (max 0.0 (- nominal-start left-overlap)))
             (segment-stop (min duration (+ nominal-stop right-overlap)))
             (segment-duration (- segment-stop segment-start)))
        (when (> segment-duration 0.0)
          (let* ((pulse (get-pulse sig segment-start segment-stop))
                 (semitones (pitch-for-pulse index))
                 (shifted (shift-pulse pulse segment-duration semitones))
                 (window
                   (pulse-crossfade-envelope
                     index pulse-count nominal-duration
                     segment-duration crossfade))
                 (windowed
                   (extract-abs 0 segment-duration
                     (cue (mult shifted window)))))
            (setf output
              (sim
                (at 0 (cue output))
                (at-abs segment-start (cue windowed))))))))))

(defun waveform-source ()
  ;; Start sine and triangle near their trough so the pitch boundary occurs at
  ;; a naturally quieter point when depth is high.
  (cond
    ((= waveform 0)
     (hzosc trem-rate *sine-table* -90.0))

    ((= waveform 1)
     (hzosc trem-rate *tri-table* -90.0))

    ((= waveform 2)
     (osc-pulse trem-rate 0.0))

    ((= waveform 3)
     (hzosc trem-rate *saw-table* 180.0))

    (t
     (mult -1.0 (hzosc trem-rate *saw-table* 180.0)))))

(defun make-tremolo-envelope (period)
  (let* ((depth (/ trem-depth 100.0))
         (minimum-power (- 1.0 depth))
         (normalized (sum 0.5 (mult 0.5 (waveform-source))))
         (smooth-time
           (min (/ waveform-smoothing 1000.0)
                (* period 0.20)))
         ;; SND-CHASE turns hard-switching control signals into short ramps.
         ;; Only the discontinuous Square and Saw modes need it.
         (smoothed
           (if (and (>= waveform 2) (> smooth-time 0.0))
               (snd-chase normalized smooth-time smooth-time)
               normalized))
         ;; Treat Depth as a power change and convert it to amplitude. This
         ;; keeps the tremolo peaks at unity, still reaches silence at 100%,
         ;; and avoids the excessive average-volume loss of linear modulation.
         (power-envelope
           (sum minimum-power (mult depth smoothed))))
    (s-sqrt power-envelope)))

(defun process-channel (sig duration period pulse-count)
  (let* ((pitched (assemble-pitched-pulses sig duration period pulse-count))
         (tremolo (make-tremolo-envelope period))
         (modulated (mult pitched tremolo))
         ;; Optional headroom protects against PITSHIFT overshoot. It defaults
         ;; to 0 dB so the plug-in does not apply an automatic level cut.
         (output (scale-db (- output-headroom) modulated)))
    (extract-abs 0 duration (cue output))))

(let* ((duration (get-duration 1))
       (period (/ 1.0 trem-rate))
       (raw-pulse-count
         (max 1 (truncate (+ (* duration trem-rate) 0.999999))))
       (last-duration
         (- duration (* (- raw-pulse-count 1) period)))
       ;; Merge a microscopic final fragment into the previous pulse. Tiny
       ;; pitch-shifted fragments are a common source of ticks and grain noise.
       (pulse-count
         (if (and (> raw-pulse-count 1)
                  (< last-duration (min 0.005 (* period 0.10))))
             (- raw-pulse-count 1)
             raw-pulse-count)))
  (cond
    ((<= duration 0.0)
     "Select some audio before running Tremolo Pitch Shift.")

    ((> duration +maximum-duration+)
     (format nil
             "For stability, Tremolo Pitch Shift is limited to ~a seconds per application. Your selection is about ~a seconds. Process it in shorter sections."
             +maximum-duration+
             duration))

    ((> pulse-count +maximum-pulses+)
     (format nil
             "This selection would create ~a pulses. The stability limit is ~a. Reduce the selection length or tremolo rate."
             pulse-count
             +maximum-pulses+))

    ((and (= pitch-mode 5) (>= random-min random-max))
     "Random minimum must be lower than Random maximum.")

    (t
     (multichan-expand #'process-channel
                       *track*
                       duration
                       period
                       pulse-count))))
