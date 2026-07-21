;nyquist plug-in
;version 4
;type process
;name "Progressive Pitch Delay"
;preview linear
;action "Creating progressive pitch delay..."
;author "jwesters / OpenAI [vibe coded]"
;release 1.0.0
;copyright "MIT License"

;control delay-mode "Delay repeats" choice "Normal,Reversed" 1
;control pitch-direction "Pitch direction" choice "Higher,Lower" 1
;control semitones "Pitch change per repeat" float "semitones" 1.0 0.0 12.0
;control repeat-count "Number of repeats" int "" 5 1 12
;control delay-time "Delay time between repeats" float "seconds" 0.35 0.01 5.0
;control dry-db "Original audio level" float "dB" 0.0 -30.0 0.0
;control first-repeat-db "First repeat level" float "dB" -6.0 -30.0 0.0
;control decay-db "Additional reduction per repeat" float "dB" 3.0 0.0 12.0
;control extend-output "Allow audio to extend past selection" choice "Yes,No" 0
;control headroom "Automatic clipping protection" choice "On,Off" 0

;; Progressive Pitch Delay 1.0
;;
;; Applies normal or reversed repeats to the selected audio. Each repeat is
;; shifted progressively farther from the original pitch while preserving
;; its duration. For example, a 2-semitone downward setting produces repeats
;; at -2, -4, -6, -8 semitones, and so on.
;;
;; Reversing audio in Nyquist requires loading the selected samples into
;; memory. A sample-count safeguard is included to reduce the risk of Audacity
;; exhausting available memory on very long selections.

(setf *max-reverse-samples* 5000000)

(defun reverse-array-in-place (data size)
  "Reverse SIZE elements of DATA and return DATA."
  (do ((left 0 (1+ left))
       (right (1- size) (1- right)))
      ((>= left right) data)
    (let ((temp (aref data left)))
      (setf (aref data left) (aref data right))
      (setf (aref data right) temp))))

(defun reverse-sound (sig sample-count)
  "Return a reversed copy of mono sound SIG."
  (let* ((sig-copy (snd-copy sig))
         (samples (snd-fetch-array sig-copy sample-count sample-count)))
    (reverse-array-in-place samples sample-count)
    (snd-from-array 0 *sound-srate* samples)))

(defun pitch-ratio-for-repeat (repeat-number)
  "Return the pitch ratio for REPEAT-NUMBER."
  (let* ((direction (if (= pitch-direction 0) 1.0 -1.0))
         (shift (* direction semitones repeat-number)))
    (power 2.0 (/ shift 12.0))))

(defun repeat-gain (repeat-number)
  "Return linear gain for REPEAT-NUMBER."
  (db-to-linear
    (- first-repeat-db
       (* decay-db (1- repeat-number)))))

(defun calculate-gain-sum ()
  "Conservative maximum gain if all sounds overlap at full amplitude."
  (let ((total (db-to-linear dry-db)))
    (do ((repeat-number 1 (1+ repeat-number)))
        ((> repeat-number repeat-count) total)
      (setf total (+ total (repeat-gain repeat-number))))))

(defun clipping-protection-scale ()
  "Return a conservative scaling factor that provides a little headroom."
  (if (= headroom 0)
      (let ((gain-sum (calculate-gain-sum)))
        (if (> gain-sum 0.98)
            (/ 0.98 gain-sum)
            1.0))
      1.0))

(defun pitch-shift-hq (sig duration ratio)
  "Pitch-shift SIG by RATIO without changing its duration."
  (if (= semitones 0.0)
      sig
      (let ((stretch-function (const 1.0))
            (pitch-function (const ratio)))
        (pv-time-pitch sig stretch-function pitch-function duration))))

(defun make-repeat (source repeat-number duration)
  "Create one progressively pitch-shifted repeat."
  (let* ((ratio (pitch-ratio-for-repeat repeat-number))
         (gain (repeat-gain repeat-number))
         (shifted (pitch-shift-hq source duration ratio)))
    (mult gain shifted)))

(defun build-delay-channel (sig duration sample-count)
  "Build the delay effect for one mono channel."
  (let* ((repeat-source
           (if (= delay-mode 1)
               (reverse-sound sig sample-count)
               sig))
         (dry (mult (db-to-linear dry-db) sig))
         (echoes (s-rest 0)))
    (do ((repeat-number 1 (1+ repeat-number)))
        ((> repeat-number repeat-count)
         (mult (clipping-protection-scale)
               (sim dry echoes)))
      (let* ((start-time (* repeat-number delay-time))
             (one-repeat
               (make-repeat repeat-source repeat-number duration)))
        (setf echoes
              (sim (at 0 (cue echoes))
                   (at-abs start-time (cue one-repeat))))))))

(defun trim-to-selection (sig duration)
  "Trim SIG to the original selected duration."
  (extract-abs 0 duration (cue sig)))

(let* ((duration (get-duration 1))
       (sample-count (truncate len)))
  (cond
    ((<= sample-count 0)
     "Please select some audio before running Progressive Pitch Delay.")

    ((and (= delay-mode 1)
          (> sample-count *max-reverse-samples*))
     (format nil
       "The selection is too long for reversed repeats in Nyquist.~%~%
Select no more than about ~a seconds at this track's sample rate, or use Normal repeats."
       (truncate (/ *max-reverse-samples* *sound-srate*))))

    (t
     (let ((output
             (multichan-expand
               #'build-delay-channel *track* duration sample-count)))
       (if (= extend-output 1)
           (multichan-expand #'trim-to-selection output duration)
           output)))))
