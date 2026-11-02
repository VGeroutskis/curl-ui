# Πού έμεινε η δουλειά

Κατάσταση στις 2026-11-02. Οι κανόνες εργασίας και οι εντολές build είναι στο `CLAUDE.md`.

## Κατάσταση

- Τελευταία έκδοση: **v0.5.0**, δημοσιευμένη με 15 αρχεία. Το CI και το release πέρασαν.
- Το `main` περιέχει το v0.5.0 (`cd88539`). Το CI και στο `main` πέρασε.
- Το `api-client` είναι στο ίδιο σημείο με το `main`. Το τοπικό `main` ενός παλιού clone θέλει `git pull`.

## Τι έγινε στην πιο πρόσφατη περίοδο

- File picker (`src/ui/view_picker.c`, `src/core/fsdir.*`), Browse σε Body File και σε γραμμές φόρμας,
  "Open file" στο Import, κάρτες επιλογών με open/save/dir, `@path`, `name@`, form και `cert:password`.
- Δικά της κουμπιά παραθύρου, μετακίνηση και αλλαγή μεγέθους (`win_hit_test`). Ρύθμιση "System title bar"
  (`native_frame`). Το AppImage ξαναξεκινά μία φορά με το libdecor του συστήματος μέσω `LD_PRELOAD`.
- Δεξί πάνελ: docs στο Curl με Hide/Show, "This request" αλλού (`src/ui/view_context.c`).
- Εισαγωγή OpenAPI 3.x, Swagger 2.0 και 1.x: `src/core/yaml.*`, `oas_load.*`, `oas_model.*`,
  `oas_read1/2/3.c`, `oas_import.*`, `app_import_openapi` στο `store.c`.
  Σχεδίαση: `docs/superpowers/specs/2026-11-02-openapi-import-design.md`.
  Πλάνο: `docs/superpowers/plans/2026-11-02-openapi-import.md`.

Έλεγχος σε πραγματικές προδιαγραφές: Stripe 612, GitHub 1254, Kubernetes 1172 αιτήματα, περίπου σε 2
δευτερόλεπτα. Δεν είναι στο repo, κατεβαίνουν από:

- https://raw.githubusercontent.com/stripe/openapi/master/openapi/spec3.yaml
- https://raw.githubusercontent.com/github/rest-api-description/main/descriptions/api.github.com/api.github.com.json
- https://raw.githubusercontent.com/kubernetes/kubernetes/master/api/openapi-spec/swagger.json

Για δοκιμή με το χέρι: `docs/samples/try-me-httpbin.yaml`, που τρέχει ενάντια στο httpbin.org.

## Επόμενα βήματα

1. **Απόφαση του χρήστη που εκκρεμεί:** να φύγει το κίτρινο χρώμα από την ειδοποίηση επιτυχούς εισαγωγής
   και το περιττό `-X GET` από τα GET που εισάγονται. Η σύστασή μου είναι ναι και στα δύο, σε επόμενη έκδοση.
2. **Δοκιμή με το χέρι, σε πραγματική οθόνη:** παράθυρο για μεταβλητές που λείπουν μετά από εισαγωγή,
   μεταβλητές στο πάνελ, Browse με πραγματικό αρχείο.
3. **Φάση 2 του API client:** εισαγωγή συλλογών Postman. Δεν έχει ξεκινήσει.

## Γνωστά ανοιχτά

Εισαγωγή OpenAPI:
- Αλλαγές γραμμής σε ονόματα από block scalars.
- Τα κλειδιά YAML `? ` απορρίπτονται, όπως στην προδιαγραφή του Twilio.
- Γραμμή σχολίου με `\t#` απορρίπτεται.
- Αρχείο κομμένο ακριβώς σε όριο γραμμής δεν ανιχνεύεται.

Πλατφόρμες, που δεν έχουν ελεγχθεί:
- Windows και macOS δεν έχουν tests στο CI. Ο κώδικας Windows του `fs_is_regular` δεν έχει τρέξει ποτέ.
- Στα Windows, το maximize παραθύρου χωρίς πλαίσιο ίσως καλύπτει το taskbar.
- Η αναζήτηση του libdecor δοκιμάστηκε μόνο σε Fedora.

Παλιότερα και μικρά:
- Secrets στο όριο των 4096 bytes.
- Μετακίνηση ή μετονομασία συλλογής μόνο για ανάγνωση.
- `-H 'Accept:'`, `?flag` που γίνεται `?flag=`, εισαγωγή με `-d ''`.
- Το modal με πάνω από περίπου 11 μεταβλητές δεν κυλά. Εστίαση μετά από μεγάλη επικόλληση.
- Η κίτρινη κουκκίδα στην καρτέλα "Send a form with a file" δεν αναπαράχθηκε.
- `queue destroyed while proxies still attached`: εμφανίστηκε σε kill, άγνωστο αν εμφανίζεται σε κανονικό κλείσιμο.

Τα μηνύματα τερματικού `Couldn't open plugin directory` και `No plugins found` είναι αβλαβή με τη δική μας μπάρα.

## Χρήσιμα στη δοκιμή

- Hooks του app: `--import-openapi FILE`, `--import-dialog`, `--double-click`, `--native-frame`,
  `--docs NAME`, `--screenshot FILE`.
- Χωρίς οθόνη: `SDL_VIDEODRIVER=dummy`.
- Όταν δοκιμάζεις AppImage, το `LD_LIBRARY_PATH` πρέπει να είναι κενό, αλλιώς το αποτέλεσμα δεν είναι έγκυρο.
