(require 'company)
(require 'company-template)

(eval-when-compile (require 'rtags))

(defvar company-rtags-modes '(c-mode c++-mode objc-mode)
  "Major modes which rtags may complete.")

(defcustom company-rtags-begin-after-member-access t
  "When non-nil, automatic completion will start whenever the current
symbol is preceded by \".\", \"->\" or \"::\", ignoring
`company-minimum-prefix-length'.

If `company-begin-commands' is a list, it should include `c-electric-lt-gt'
and `c-electric-colon', for automatic completion right after \">\" and
\":\"."
  :group 'rtags
  :type 'boolean)

(defcustom company-rtags-max-wait 100
  "Max number of waits company-rtags will do before giving up (max wait time is (* company-rtags-max-wait company-async-wait))"
  :group 'rtags
  :type 'integer)

(defcustom company-rtags-use-async t
  "Whether to use async completions for company-rtags"
  :group 'rtags
  :type 'boolean)

(defcustom company-rtags-insert-arguments t
  "When non-nil, insert function arguments as a template after completion."
  :group 'rtags
  :type 'boolean)

(defun company-rtags--prefix ()
  (let ((symbol (company-grab-symbol)))
    (if symbol
        (if (and company-rtags-begin-after-member-access
                 (save-excursion
                   (forward-char (- (length symbol)))
                   (looking-back "\\.\\|->\\|::" (- (point) 2))))
            (cons symbol t)
          symbol)
      'stop)))

(defun company-rtags--make-candidate (candidate maxwidth)
  (let* ((text (copy-sequence (nth 0 candidate)))
         (meta (nth 1 candidate))
         (metalength (length meta)))
    (if (> metalength maxwidth)
        (setq meta (concat (substring meta 0 (- maxwidth 5)) "<...>)")))
    (put-text-property 0 1 'meta meta text)
    text))

(defun company-rtags--candidates (prefix)
  (when (rtags-has-diagnostics)
    (let ((updated (rtags-update-completions)))
      (when updated
        (if (numberp updated)
            (let ((old rtags-last-completions)
                  (maxwait company-rtags-max-wait))
              (while (and (eq old rtags-last-completions)
                          (> maxwait 0))
                (decf maxwait)
                (sleep-for company-async-wait))))
        (if (and rtags-last-completions
                 (eq (current-buffer) (car rtags-last-completion-position))
                 (= (or (rtags-calculate-completion-point) -1) (cdr rtags-last-completion-position)))
            (let (results
                  (candidates (cadr rtags-last-completions))
                  (maxwidth (- (window-width) (- (point) (point-at-bol)))))
              (while candidates
                (if (or (not prefix) (string-prefix-p prefix (caar candidates)))
                    (push (company-rtags--make-candidate (car candidates) maxwidth) results))
                (setq candidates (cdr candidates)))
              (reverse results)))))))

(defun company-rtags--meta (candidate)
  (get-text-property 0 'meta candidate))

(defun company-rtags--annotation (candidate)
  (let ((meta (company-rtags--meta candidate)))
    (cond
     ((null meta) nil)
     ((string-match "\\((.*)\\)" meta)
      (match-string 1 meta)))))

(defvar rtags-company-last-completion-position nil)
(defvar rtags-company-last-completion-callback nil)
(defvar rtags-company-last-completion-prefix nil)
(defun rtags-company-update-completions (cb)
  ;; (setq rtags-company-last-completion-prefix prefix)
  (setq rtags-company-last-completion-callback cb)
  (rtags-update-completions)
  (setq rtags-company-last-completion-position rtags-last-completion-position)
  (rtags-company-diagnostics-hook))

(defun rtags-company-diagnostics-hook ()
  (when (and (eq (car rtags-last-completion-position) (car rtags-company-last-completion-position))
             (= (cdr rtags-last-completion-position) (cdr rtags-company-last-completion-position)))
    (let ((results nil)
          (maxwidth (max 10 (- (window-width) (- (point) (point-at-bol)))))
          (candidates (cadr rtags-last-completions)))
      (while candidates
        (when (or (not rtags-company-last-completion-prefix)
                  (string-prefix-p rtags-company-last-completion-prefix (caar candidates)))
          (push (company-rtags--make-candidate (car candidates) maxwidth) results))
        (setq candidates (cdr candidates)))
      (funcall rtags-company-last-completion-callback (reverse results)))))
(add-hook 'rtags-diagnostics-hook 'rtags-company-diagnostics-hook)

(defun company-rtags (command &optional arg &rest ignored)
  "`company-mode' completion back-end for `rtags'."
  (interactive (list 'interactive))
  (setq rtags-company-last-completion-prefix arg)
  (case command
    (interactive (company-begin-backend 'company-rtags))
    (prefix (and (rtags-is-indexed)
                 (memq major-mode company-rtags-modes)
                 buffer-file-name
                 (not (company-in-string-or-comment))
                 (company-rtags--prefix)))
    (candidates
     (if company-rtags-use-async
         (cons :async
               (lambda (cb)
                 (rtags-company-update-completions cb)))
       (candidates (company-rtags--candidates arg))))
    (meta (company-rtags--meta arg))
    (sorted t)
    (annotation (company-rtags--annotation arg))
    (post-completion (let ((anno (company-rtags--annotation arg)))
                       (when (and company-rtags-insert-arguments anno)
                         (insert anno)
                         (company-template-c-like-templatify anno))))))

(provide 'company-rtags)
