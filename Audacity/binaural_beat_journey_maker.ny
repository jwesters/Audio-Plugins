;nyquist plug-in
;version 4
;type tool
;name "Binaural Beat Journey Maker"
;author "jwesters / OpenAI [vibe coded]"
;release 1.2.0
;copyright "Released for personal and educational use"
;preview disabled
;action "Rendering and importing two mono binaural journey tracks..."
;control text "This version automatically creates TWO NEW mono tracks. No track needs to be selected first."
;control text "Stereo headphones are required for the binaural effect. Journey durations and transitions are entered in seconds."
;control text "The left and right mono tracks are initially panned fully left and right, and can be adjusted independently afterward."
;control text "Rendering long journeys requires temporary disk space. Temporary WAV files are cleared after Audacity imports them."
;control preset "Journey preset" choice "Custom stages,Sleep Journey - 2580 sec,Focus Journey - 1620 sec,Meditation Journey - 2160 sec" 0

;control text "Musical anchor"
;control key "Key" choice "C major,C minor,C# major,C# minor,D major,D minor,D# major,D# minor,E major,E minor,F major,F minor,F# major,F# minor,G major,G minor,G# major,G# minor,A major,A minor,A# major,A# minor,B major,B minor" 0
;control degree "Anchor note - scale degree" choice "1 - tonic,2,3,4,5,6,7" 0
;control octavesel "Octave" choice "2 - low,3,4 - medium,5 - high,6 - very high" 2
;control manualanchor "Use exact custom anchor" choice "No,Yes" 0
;control manualhz "Custom anchor frequency" float-text "Hz" 261.63 20.0 20000.0
;control anchorear "Anchor ear" choice "Left ear,Right ear" 0

;control text "Sound and stereo channels"
;control waveform "Waveform" choice "Sine - pure and smooth,Triangle - softer harmonics,Square - bright,Sawtooth - buzzy" 0
;control tonevol "Tone volume" float "percent" 25.0 0.0 60.0
;control leftvol "Left channel volume" float "percent" 100.0 0.0 100.0
;control rightvol "Right channel volume" float "percent" 100.0 0.0 100.0
;control noisetype "Ambient noise" choice "None,Pink noise,Brown noise" 0
;control noisevol "Ambient volume" float "percent" 15.0 0.0 60.0
;control fadein "Fade in" float-text "seconds" 3.0 0.0 30.0
;control fadeout "Fade out" float-text "seconds" 3.0 0.0 30.0

;control text "Custom Stage 1 - always enabled"
;control s1type "Stage 1 beat type" choice "Delta - 2 Hz,Theta - 6 Hz,Schumann - 7.83 Hz,Alpha - 10 Hz,Beta - 18 Hz,Gamma - 40 Hz,Custom" 4
;control s1hz "Stage 1 custom beat" float-text "Hz" 18.0 0.1 100.0
;control s1hold "Stage 1 hold" float-text "seconds" 300.0 0.1 3600.0
;control s1trans "Stage 1 transition to next" float-text "seconds" 120.0 0.0 1800.0

;control text "Custom Stage 2"
;control s2on "Enable Stage 2" choice "Off,On" 1
;control s2type "Stage 2 beat type" choice "Delta - 2 Hz,Theta - 6 Hz,Schumann - 7.83 Hz,Alpha - 10 Hz,Beta - 18 Hz,Gamma - 40 Hz,Custom" 5
;control s2hz "Stage 2 custom beat" float-text "Hz" 40.0 0.1 100.0
;control s2hold "Stage 2 hold" float-text "seconds" 300.0 0.1 3600.0
;control s2trans "Stage 2 transition to next" float-text "seconds" 180.0 0.0 1800.0

;control text "Custom Stage 3"
;control s3on "Enable Stage 3" choice "Off,On" 1
;control s3type "Stage 3 beat type" choice "Delta - 2 Hz,Theta - 6 Hz,Schumann - 7.83 Hz,Alpha - 10 Hz,Beta - 18 Hz,Gamma - 40 Hz,Custom" 0
;control s3hz "Stage 3 custom beat" float-text "Hz" 2.0 0.1 100.0
;control s3hold "Stage 3 hold" float-text "seconds" 600.0 0.1 3600.0
;control s3trans "Stage 3 transition to next" float-text "seconds" 0.0 0.0 1800.0

;control text "Custom Stage 4"
;control s4on "Enable Stage 4" choice "Off,On" 0
;control s4type "Stage 4 beat type" choice "Delta - 2 Hz,Theta - 6 Hz,Schumann - 7.83 Hz,Alpha - 10 Hz,Beta - 18 Hz,Gamma - 40 Hz,Custom" 3
;control s4hz "Stage 4 custom beat" float-text "Hz" 10.0 0.1 100.0
;control s4hold "Stage 4 hold" float-text "seconds" 300.0 0.1 3600.0
;control s4trans "Stage 4 transition to next" float-text "seconds" 120.0 0.0 1800.0

;control text "Custom Stage 5"
;control s5on "Enable Stage 5" choice "Off,On" 0
;control s5type "Stage 5 beat type" choice "Delta - 2 Hz,Theta - 6 Hz,Schumann - 7.83 Hz,Alpha - 10 Hz,Beta - 18 Hz,Gamma - 40 Hz,Custom" 1
;control s5hz "Stage 5 custom beat" float-text "Hz" 6.0 0.1 100.0
;control s5hold "Stage 5 hold" float-text "seconds" 300.0 0.1 3600.0
;control s5trans "Stage 5 transition to next" float-text "seconds" 120.0 0.0 1800.0

;control text "Custom Stage 6"
;control s6on "Enable Stage 6" choice "Off,On" 0
;control s6type "Stage 6 beat type" choice "Delta - 2 Hz,Theta - 6 Hz,Schumann - 7.83 Hz,Alpha - 10 Hz,Beta - 18 Hz,Gamma - 40 Hz,Custom" 0
;control s6hz "Stage 6 custom beat" float-text "Hz" 2.0 0.1 100.0
;control s6hold "Stage 6 hold" float-text "seconds" 300.0 0.1 3600.0
;control s6trans "Stage 6 transition - unused" float-text "seconds" 0.0 0.0 1800.0

;; ---------------------------------------------------------------------------
;; Helpers
;; ---------------------------------------------------------------------------

(defun beat-frequency (beat-type custom-hz)
  (cond ((= beat-type 0) 2.0)
        ((= beat-type 1) 6.0)
        ((= beat-type 2) 7.83)
        ((= beat-type 3) 10.0)
        ((= beat-type 4) 18.0)
        ((= beat-type 5) 40.0)
        (t custom-hz)))

(defun musical-anchor-frequency ()
  (if (= manualanchor 1)
      manualhz
      (let* ((root (truncate (/ key 2.0)))
             (minor-mode (= (rem key 2) 1))
             (major-intervals '(0 2 4 5 7 9 11))
             (minor-intervals '(0 2 3 5 7 8 10))
             (intervals (if minor-mode minor-intervals major-intervals))
             ;; The website keeps the selected octave when a scale wraps.
             (note-class (rem (+ root (nth degree intervals)) 12))
             (octave (+ octavesel 2))
             (midi (+ note-class (* 12 (+ octave 1)))))
        (* 440.0 (power 2.0 (/ (- midi 69.0) 12.0))))))

(defun custom-stage-list ()
  (let ((stages
          (list (list (beat-frequency s1type s1hz) s1hold s1trans))))
    (if (= s2on 1)
        (setq stages (append stages
                             (list (list (beat-frequency s2type s2hz)
                                         s2hold s2trans)))))
    (if (= s3on 1)
        (setq stages (append stages
                             (list (list (beat-frequency s3type s3hz)
                                         s3hold s3trans)))))
    (if (= s4on 1)
        (setq stages (append stages
                             (list (list (beat-frequency s4type s4hz)
                                         s4hold s4trans)))))
    (if (= s5on 1)
        (setq stages (append stages
                             (list (list (beat-frequency s5type s5hz)
                                         s5hold s5trans)))))
    (if (= s6on 1)
        (setq stages (append stages
                             (list (list (beat-frequency s6type s6hz)
                                         s6hold s6trans)))))
    stages))

(defun selected-stage-list ()
  (cond
    ;; Sleep: Alpha 300s, glide 180s, Theta 600s, glide 300s, Delta 1200s.
    ((= preset 1)
     '((10.0 300.0 180.0)
       (6.0 600.0 300.0)
       (2.0 1200.0 0.0)))
    ;; Focus: Alpha 180s, glide 120s, Beta 900s, glide 120s, Gamma 300s.
    ((= preset 2)
     '((10.0 180.0 120.0)
       (18.0 900.0 120.0)
       (40.0 300.0 0.0)))
    ;; Meditation: Alpha 300s, glide 180s, Schumann 600s, glide 180s, Theta 900s.
    ((= preset 3)
     '((10.0 300.0 180.0)
       (7.83 600.0 180.0)
       (6.0 900.0 0.0)))
    (t (custom-stage-list))))

(defun journey-duration (stages)
  (let ((total 0.0)
        (count (length stages)))
    (dotimes (i count total)
      (setq total (+ total (nth 1 (nth i stages))))
      (if (< i (- count 1))
          (setq total (+ total (nth 2 (nth i stages))))))))

(defun maximum-beat (stages)
  (let ((highest 0.0))
    (dotimes (i (length stages) highest)
      (setq highest (max highest (nth 0 (nth i stages)))))))

(defun beat-envelope (stages)
  (let* ((points (list (nth 0 (nth 0 stages))))
         (elapsed 0.0)
         (count (length stages)))
    (dotimes (i count)
      (let* ((stage (nth i stages))
             (beat (nth 0 stage))
             (hold (nth 1 stage))
             (transition (nth 2 stage)))
        ;; Hold the current beat frequency.
        (setq elapsed (+ elapsed hold))
        (setq points (append points (list elapsed beat)))
        ;; Glide to the next enabled stage. Simultaneous points create the
        ;; steepest possible step when transition is zero.
        (if (< i (- count 1))
            (let ((next-beat (nth 0 (nth (+ i 1) stages))))
              (setq elapsed (+ elapsed transition))
              (setq points (append points (list elapsed next-beat)))))))
    (pwlv-list points)))

(defun oscillator-for-waveform (frequency-signal)
  (cond ((= waveform 0) (hzosc frequency-signal))
        ((= waveform 1) (osc-tri frequency-signal))
        ((= waveform 2) (osc-pulse frequency-signal 0.0))
        (t (osc-saw frequency-signal))))

(defun pink-noise (dur)
  ;; Paul Kellet-style parallel filters, adapted for Nyquist.
  (let ((white (abs-env (noise dur))))
    (mult (db-to-linear (- (+ 15.0 (power dur 0.1))))
          (sim
            (biquad white 0.0555179 0 0 1 0.99886 0)
            (biquad white 0.0750759 0 0 1 0.99332 0)
            (biquad white 0.1538520 0 0 1 0.96900 0)
            (biquad white 0.3104856 0 0 1 0.86650 0)
            (biquad white 0.5329522 0 0 1 0.55000 0)
            (mult -1.0
                  (biquad white 0.0168980 0 0 1 -0.7616 0))))))

(defun brown-noise (dur)
  ;; A first-order low-pass gives the approximately 6 dB/octave slope of
  ;; brown noise. The gain compensates for the low filter output level.
  (mult 6.0 (lp (abs-env (noise dur)) 100.0)))

(defun ambient-noise (dur)
  (cond ((= noisetype 1) (pink-noise dur))
        ((= noisetype 2) (brown-noise dur))
        (t (const 0.0 dur))))

(defun fade-envelope (dur)
  (let ((fi (min fadein (/ dur 2.0)))
        (fo (min fadeout (/ dur 2.0))))
    (cond ((and (= fi 0.0) (= fo 0.0))
           (const 1.0 dur))
          ((= fi 0.0)
           (pwlv 1.0 (- dur fo) 1.0 dur 0.0))
          ((= fo 0.0)
           (pwlv 0.0 fi 1.0 dur 1.0))
          (t
           (pwlv 0.0 fi 1.0 (- dur fo) 1.0 dur 0.0)))))

;; ---------------------------------------------------------------------------
;; Render two temporary mono WAV files and import them as separate Audacity
;; tracks. This gives the user independent gain, effects, and pan control for
;; the left and right binaural channels.
;; ---------------------------------------------------------------------------

(defun make-temp-filename (channel-name)
  (let* ((directory (get '*system-dir* 'sys-temp))
         (year (get '*system-time* 'year))
         (day (get '*system-time* 'day))
         (suffix (random 1000000))
         (filename
           (format nil "binaural_beat_journey_~a_~a_~a_~a.wav"
                   channel-name year day suffix)))
    (if (or (char= (char directory (- (length directory) 1)) *file-separator*)
            (char= (char directory (- (length directory) 1)) #\/))
        (strcat directory filename)
        ;; Audacity's bundled XLISP FORMAT supports ~a but not the Common
        ;; Lisp ~c character directive. Formatting the separator with ~a
        ;; converts the character to a one-character string safely.
        (strcat directory (format nil "~a" *file-separator*) filename))))

(defun clear-temp-file (filename)
  ;; Audacity's Nyquist sandbox does not provide a normal delete-file command.
  ;; Reopening for output and writing an empty string truncates the file to zero
  ;; bytes after Audacity has copied the imported audio into the project.
  (let ((stream (open filename :direction :output)))
    (if stream
        (progn
          (format stream "")
          (close stream)
          t)
        nil)))

(defun build-mono-channel (stages duration anchor channel)
  ;; CHANNEL 0 is the left-ear track and CHANNEL 1 is the right-ear track.
  ;; Each channel builds its own control and noise signals so the sounds can be
  ;; saved independently without sharing a consumed Nyquist sound stream.
  (let* ((beat-control (beat-envelope stages))
         (anchor-control (const anchor duration))
         (frequency
           (cond
             ((and (= channel 0) (= anchorear 0)) anchor-control)
             ((and (= channel 1) (= anchorear 1)) anchor-control)
             (t (sum anchor-control beat-control))))
         (tone (oscillator-for-waveform frequency))
         (tone-amplitude (/ tonevol 100.0))
         (channel-amplitude
           (* tone-amplitude
              (if (= channel 0)
                  (/ leftvol 100.0)
                  (/ rightvol 100.0))))
         (ambient-amplitude (/ noisevol 100.0))
         (noise-signal (ambient-noise duration))
         (mix
           (sum (mult channel-amplitude tone)
                (mult ambient-amplitude noise-signal)))
         (fade (fade-envelope duration)))
    (clip (mult fade mix) 0.99)))

(defun save-mono-wave (sound filename)
  ;; 16-bit PCM keeps temporary storage reasonable while retaining ample
  ;; quality for generated tones and ambient noise. Do not pass :swap here:
  ;; Audacity's bundled S-SAVE wrapper does not expose that keyword.
  (s-save sound ny:all filename
          :format snd-head-wave
          :mode snd-mode-pcm
          :bits 16
          :play nil))

(defun select-one-track (track-index)
  (aud-do-command "SelectTracks"
                  :track track-index
                  :trackcount 1
                  :mode "Set"))

(defun configure-mono-track (track-index track-name pan-value)
  (select-one-track track-index)
  (aud-do-command "SetTrack"
                  :name track-name
                  :selected t
                  :focused t
                  :pan pan-value))

(let* ((stages (selected-stage-list))
       (duration (journey-duration stages))
       (anchor (musical-anchor-frequency))
       (highest-beat (maximum-beat stages))
       (highest-frequency (+ anchor highest-beat))
       (project-rate-value (get '*project* 'rate)))
  (cond
    ((<= duration 0.0)
     "The journey must have a duration greater than zero seconds.")
    ((> duration 7200.0)
     "The total journey is longer than 7200 seconds (2 hours). Shorten one or more enabled stages to reduce temporary disk use.")
    ((or (null project-rate-value) (<= project-rate-value 0.0))
     "Audacity did not provide a valid project sample rate.")
    ((>= highest-frequency (* 0.48 project-rate-value))
     "The anchor plus beat frequency is too high for the current Audacity project sample rate. Lower the anchor frequency or use a higher project sample rate.")
    (t
     ;; Tool plug-ins do not require a selected track, so use the project rate
     ;; explicitly for all generated audio and control signals.
     (let ((project-rate (float project-rate-value)))
       (setf *sound-srate* project-rate)
       (setf *control-srate* (/ project-rate 20.0))
       (let* ((left-file (make-temp-filename "left"))
              (right-file (make-temp-filename "right"))
              (track-count-before (length (aud-get-info "tracks")))
              (left-sound
                (abs-env (build-mono-channel stages duration anchor 0)))
              (left-render-peak
                (save-mono-wave left-sound left-file))
              (right-sound
                (abs-env (build-mono-channel stages duration anchor 1)))
              (right-render-peak
                (save-mono-wave right-sound right-file))
              (left-import
                (aud-do-command "Import2" :filename left-file)))
         (if (not (cdr left-import))
             (format nil
                     "Both mono WAV files were rendered, but Audacity could not import the left channel. The files are located at:~%~a~%~a~%~%Audacity response: ~a"
                     left-file right-file (car left-import))
             (progn
               (configure-mono-track
                 track-count-before
                 "Binaural Beat Journey - Left"
                 -1.0)
               (let* ((right-track-index (length (aud-get-info "tracks")))
                      (right-import
                        (aud-do-command "Import2" :filename right-file)))
                 (if (not (cdr right-import))
                     (format nil
                             "The left mono track was imported, but Audacity could not import the right channel. The right WAV is located at:~%~a~%~%Audacity response: ~a"
                             right-file (car right-import))
                     (progn
                       (configure-mono-track
                         right-track-index
                         "Binaural Beat Journey - Right"
                         1.0)
                       ;; Leave both new tracks selected so they are easy to
                       ;; move, align, or apply a shared effect to immediately.
                       (aud-do-command "SelectTracks"
                                       :track track-count-before
                                       :trackcount 2
                                       :mode "Set")
                       (clear-temp-file left-file)
                       (clear-temp-file right-file)
                       ""))))))))))
