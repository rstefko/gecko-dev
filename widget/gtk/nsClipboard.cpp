/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:expandtab:shiftwidth=4:tabstop=4:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/ArrayUtils.h"

#include "nsArrayUtils.h"
#include "nsClipboard.h"
#include "nsClipboardX11.h"
#if defined(MOZ_WAYLAND)
#include "nsClipboardWayland.h"
#endif
#include "nsContentUtils.h"
#include "HeadlessClipboard.h"
#include "nsSupportsPrimitives.h"
#include "nsString.h"
#include "nsReadableUtils.h"
#include "nsPrimitiveHelpers.h"
#include "nsIServiceManager.h"
#include "nsImageToPixbuf.h"
#include "nsStringStream.h"
#include "nsIObserverService.h"
#include "mozilla/Services.h"
#include "mozilla/RefPtr.h"
#include "mozilla/TimeStamp.h"

#include "imgIContainer.h"

#include <gtk/gtk.h>
#include <gtk/gtkx.h>

#include "mozilla/Encoding.h"

using namespace mozilla;

// Idle timeout for receiving selection and property notify events (microsec)
const int kClipboardTimeout = 500000;

// We add this prefix to HTML markup, so that GetHTMLCharset can correctly
// detect the HTML as UTF-8 encoded.
static const char kHTMLMarkupPrefix[] =
    R"(<meta http-equiv="content-type" content="text/html; charset=utf-8">)";

// Callback when someone asks us for the data
void
clipboard_get_cb(GtkClipboard *aGtkClipboard,
                 GtkSelectionData *aSelectionData,
                 guint info,
                 gpointer user_data);

// Callback when someone asks us to clear a clipboard
void
clipboard_clear_cb(GtkClipboard *aGtkClipboard,
                   gpointer user_data);

static void
ConvertHTMLtoUCS2          (const char*         data,
                            int32_t             dataLength,
                            char16_t         **unicodeData,
                            int32_t            &outUnicodeLen);

static void
GetHTMLCharset             (const char* data, int32_t dataLength, nsCString& str);

GdkAtom
GetSelectionAtom(int32_t aWhichClipboard)
{
    if (aWhichClipboard == nsIClipboard::kGlobalClipboard)
        return GDK_SELECTION_CLIPBOARD;

    return GDK_SELECTION_PRIMARY;
}

nsClipboard::nsClipboard()
{
}

nsClipboard::~nsClipboard()
{
    // We have to clear clipboard before gdk_display_close() call.
    // See bug 531580 for details.
    if (mGlobalTransferable) {
        gtk_clipboard_clear(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
    }
    if (mSelectionTransferable) {
        gtk_clipboard_clear(gtk_clipboard_get(GDK_SELECTION_PRIMARY));
    }
}

NS_IMPL_ISUPPORTS(nsClipboard, nsIClipboard, nsIObserver)

nsresult
nsClipboard::Init(void)
{
    GdkDisplay *display = gdk_display_get_default();

    // Create a nsRetrievalContext. If there's no default display
    // create the X11 one as a fallback.
    if (!display || GDK_IS_X11_DISPLAY(display)) {
        mContext = new nsRetrievalContextX11();
#if defined(MOZ_WAYLAND)
    } else {
        mContext = new nsRetrievalContextWayland();
#endif
    }
    NS_ASSERTION(mContext, "Missing nsRetrievalContext for nsClipboard!");

    nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
    if (os) {
        os->AddObserver(this, "quit-application", false);
        os->AddObserver(this, "xpcom-shutdown", false);
    }

    return NS_OK;
}


nsresult
nsClipboard::Store(void)
{
    if (mGlobalTransferable) {
        GtkClipboard *clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
        gtk_clipboard_store(clipboard);
    }
    return NS_OK;
}

NS_IMETHODIMP
nsClipboard::Observe(nsISupports *aSubject, const char *aTopic,
                     const char16_t *aData)
{
    Store();
    return NS_OK;
}

NS_IMETHODIMP
nsClipboard::SetData(nsITransferable *aTransferable,
                     nsIClipboardOwner *aOwner, int32_t aWhichClipboard)
{
    // See if we can short cut
    if ((aWhichClipboard == kGlobalClipboard &&
         aTransferable == mGlobalTransferable.get() &&
         aOwner == mGlobalOwner.get()) ||
        (aWhichClipboard == kSelectionClipboard &&
         aTransferable == mSelectionTransferable.get() &&
         aOwner == mSelectionOwner.get())) {
        return NS_OK;
    }

    // Clear out the clipboard in order to set the new data
    EmptyClipboard(aWhichClipboard);

    // List of suported targets
    GtkTargetList *list = gtk_target_list_new(nullptr, 0);

    // Get the types of supported flavors
    nsTArray<nsCString> flavors;
    nsresult rv = aTransferable->FlavorsTransferableCanExport(flavors);
    if (NS_FAILED(rv)) {
        return rv;
    }

    // Add all the flavors to this widget's supported type.
    bool imagesAdded = false;
    for (uint32_t i = 0; i < flavors.Length(); i++) {
        nsCString& flavorStr = flavors[i];

        // Special case text/unicode since we can handle all of the string types.
        if (flavorStr.EqualsLiteral(kUnicodeMime)) {
            gtk_target_list_add_text_targets(list, 0);
            continue;
        }

        if (nsContentUtils::IsFlavorImage(flavorStr)) {
            // Don't bother adding image targets twice
            if (!imagesAdded) {
                // accept any writable image type
                gtk_target_list_add_image_targets(list, 0, TRUE);
                imagesAdded = true;
            }
            continue;
        }

        // Add this to our list of valid targets
        GdkAtom atom = gdk_atom_intern(flavorStr.get(), FALSE);
        gtk_target_list_add(list, atom, 0, 0);
    }

    // Get GTK clipboard (CLIPBOARD or PRIMARY)
    GtkClipboard *gtkClipboard = gtk_clipboard_get(GetSelectionAtom(aWhichClipboard));

    gint numTargets;
    GtkTargetEntry *gtkTargets = gtk_target_table_new_from_list(list, &numTargets);

    // Set getcallback and request to store data after an application exit
    if (gtkTargets &&
        gtk_clipboard_set_with_data(gtkClipboard, gtkTargets, numTargets,
                                    clipboard_get_cb, clipboard_clear_cb, this))
    {
        // We managed to set-up the clipboard so update internal state
        // We have to set it now because gtk_clipboard_set_with_data() calls clipboard_clear_cb()
        // which reset our internal state
        if (aWhichClipboard == kSelectionClipboard) {
            mSelectionOwner = aOwner;
            mSelectionTransferable = aTransferable;
        }
        else {
            mGlobalOwner = aOwner;
            mGlobalTransferable = aTransferable;
            gtk_clipboard_set_can_store(gtkClipboard, gtkTargets, numTargets);
        }

        rv = NS_OK;
    }
    else {
        rv = NS_ERROR_FAILURE;
    }

    gtk_target_table_free(gtkTargets, numTargets);
    gtk_target_list_unref(list);

    return rv;
}

void
nsClipboard::SetTransferableData(nsITransferable* aTransferable,
                                 nsCString&       aFlavor,
                                 const char*      aClipboardData,
                                 uint32_t         aClipboardDataLength)
{
  nsCOMPtr<nsISupports> wrapper;
  nsPrimitiveHelpers::CreatePrimitiveForData(aFlavor,
                                             aClipboardData,
                                             aClipboardDataLength,
                                             getter_AddRefs(wrapper));
  aTransferable->SetTransferData(aFlavor.get(), wrapper);
}

NS_IMETHODIMP
nsClipboard::GetData(nsITransferable *aTransferable, int32_t aWhichClipboard)
{
    if (!aTransferable)
        return NS_ERROR_FAILURE;

    // Get a list of flavors this transferable can import
    nsTArray<nsCString> flavors;
    nsresult rv = aTransferable->FlavorsTransferableCanImport(flavors);
    if (NS_FAILED(rv)) {
        return rv;
    }

    for (uint32_t i = 0; i < flavors.Length(); i++) {
        nsCString& flavorStr = flavors[i];

        if (flavorStr.EqualsLiteral(kJPEGImageMime) ||
            flavorStr.EqualsLiteral(kJPGImageMime) ||
            flavorStr.EqualsLiteral(kPNGImageMime) ||
            flavorStr.EqualsLiteral(kGIFImageMime)) {
            // Emulate support for image/jpg
            if (flavorStr.EqualsLiteral(kJPGImageMime)) {
                flavorStr.Assign(kJPEGImageMime);
            }

            uint32_t    clipboardDataLength;
            const char* clipboardData =
                mContext->GetClipboardData(flavorStr.get(),
                                           aWhichClipboard,
                                           &clipboardDataLength);
            if (!clipboardData)
                continue;

            nsCOMPtr<nsIInputStream> byteStream;
            NS_NewByteInputStream(getter_AddRefs(byteStream),
                                  clipboardData,
                                  clipboardDataLength,
                                  NS_ASSIGNMENT_COPY);
            aTransferable->SetTransferData(flavorStr.get(), byteStream);

            mContext->ReleaseClipboardData(clipboardData);
            return NS_OK;
        }

        // Special case text/unicode since we can convert any
        // string into text/unicode
        if (flavorStr.EqualsLiteral(kUnicodeMime)) {
            const char* clipboardData =
                mContext->GetClipboardText(aWhichClipboard);
            if (!clipboardData) {
                // If the type was text/unicode and we couldn't get
                // text off the clipboard, run the next loop
                // iteration.
                continue;
            }

            // Convert utf-8 into our unicode format.
            NS_ConvertUTF8toUTF16 ucs2string(clipboardData);
            const char* unicodeData = (const char *)ToNewUnicode(ucs2string);
            uint32_t unicodeDataLength = ucs2string.Length() * 2;
            SetTransferableData(aTransferable, flavorStr,
                                unicodeData, unicodeDataLength);
            free((void *)unicodeData);

            mContext->ReleaseClipboardData(clipboardData);
            return NS_OK;
        }


        uint32_t clipboardDataLength;
        const char* clipboardData = mContext->GetClipboardData(
            flavorStr.get(), aWhichClipboard, &clipboardDataLength);

        if (clipboardData) {
            // Special case text/html since we can convert into UCS2
            if (flavorStr.EqualsLiteral(kHTMLMime)) {
                char16_t* htmlBody = nullptr;
                int32_t htmlBodyLen = 0;
                // Convert text/html into our unicode format
                ConvertHTMLtoUCS2(clipboardData, clipboardDataLength,
                                  &htmlBody, htmlBodyLen);

                // Try next data format?
                if (!htmlBodyLen) {
                    mContext->ReleaseClipboardData(clipboardData);
                    continue;
                }

                SetTransferableData(aTransferable, flavorStr,
                                    (const char*)htmlBody, htmlBodyLen * 2);
                free(htmlBody);
            } else {
                SetTransferableData(aTransferable, flavorStr,
                                    clipboardData, clipboardDataLength);
            }

            mContext->ReleaseClipboardData(clipboardData);
            return NS_OK;
        }
    }

    return NS_OK;
}

NS_IMETHODIMP
nsClipboard::EmptyClipboard(int32_t aWhichClipboard)
{
    if (aWhichClipboard == kSelectionClipboard) {
        if (mSelectionOwner) {
            mSelectionOwner->LosingOwnership(mSelectionTransferable);
            mSelectionOwner = nullptr;
        }
        mSelectionTransferable = nullptr;
    }
    else {
        if (mGlobalOwner) {
            mGlobalOwner->LosingOwnership(mGlobalTransferable);
            mGlobalOwner = nullptr;
        }
        mGlobalTransferable = nullptr;
    }

    return NS_OK;
}

NS_IMETHODIMP
nsClipboard::HasDataMatchingFlavors(const char** aFlavorList, uint32_t aLength,
                                    int32_t aWhichClipboard, bool *_retval)
{
  if (!aFlavorList || !_retval)
      return NS_ERROR_NULL_POINTER;

  *_retval = false;

  int targetNums;
  GdkAtom* targets = mContext->GetTargets(aWhichClipboard, &targetNums);
  if (!targets)
      return NS_OK;

  // Walk through the provided types and try to match it to a
  // provided type.
  for (uint32_t i = 0; i < aLength && !*_retval; i++) {
      // We special case text/unicode here.
      if (!strcmp(aFlavorList[i], kUnicodeMime) &&
          gtk_targets_include_text(targets, targetNums)) {
          *_retval = true;
          break;
      }

      for (int32_t j = 0; j < targetNums; j++) {
          gchar *atom_name = gdk_atom_name(targets[j]);
          if (!atom_name)
              continue;

          if (!strcmp(atom_name, aFlavorList[i]))
              *_retval = true;

          // X clipboard supports image/jpeg, but we want to emulate support
          // for image/jpg as well
          if (!strcmp(aFlavorList[i], kJPGImageMime) &&
              !strcmp(atom_name, kJPEGImageMime)) {
              *_retval = true;
          }

          g_free(atom_name);

          if (*_retval)
              break;
      }
  }

  g_free(targets);
  return NS_OK;
}

NS_IMETHODIMP
nsClipboard::SupportsSelectionClipboard(bool *_retval)
{
    *_retval = mContext->HasSelectionSupport();
    return NS_OK;
}

NS_IMETHODIMP
nsClipboard::SupportsFindClipboard(bool* _retval)
{
  *_retval = false;
  return NS_OK;
}

nsITransferable *
nsClipboard::GetTransferable(int32_t aWhichClipboard)
{
    nsITransferable *retval;

    if (aWhichClipboard == kSelectionClipboard)
        retval = mSelectionTransferable.get();
    else
        retval = mGlobalTransferable.get();

    return retval;
}

void
nsClipboard::SelectionGetEvent(GtkClipboard     *aClipboard,
                               GtkSelectionData *aSelectionData)
{
    // Someone has asked us to hand them something.  The first thing
    // that we want to do is see if that something includes text.  If
    // it does, try to give it text/unicode after converting it to
    // utf-8.

    int32_t whichClipboard;

    // which clipboard?
    GdkAtom selection = gtk_selection_data_get_selection(aSelectionData);
    if (selection == GDK_SELECTION_PRIMARY)
        whichClipboard = kSelectionClipboard;
    else if (selection == GDK_SELECTION_CLIPBOARD)
        whichClipboard = kGlobalClipboard;
    else
        return; // THAT AIN'T NO CLIPBOARD I EVER HEARD OF

    nsCOMPtr<nsITransferable> trans = GetTransferable(whichClipboard);
    if (!trans) {
      // We have nothing to serve
#ifdef DEBUG_CLIPBOARD
      printf("nsClipboard::SelectionGetEvent() - %s clipboard is empty!\n",
             whichClipboard == kSelectionClipboard ? "Selection" : "Global");
#endif
      return;
    }

    nsresult rv;
    nsCOMPtr<nsISupports> item;

    GdkAtom selectionTarget = gtk_selection_data_get_target(aSelectionData);

    // Check to see if the selection data is some text type.
    if (gtk_targets_include_text(&selectionTarget, 1)) {
        // Try to convert our internal type into a text string.  Get
        // the transferable for this clipboard and try to get the
        // text/unicode type for it.
        rv = trans->GetTransferData("text/unicode", getter_AddRefs(item));
        if (!item || NS_FAILED(rv))
            return;

        nsCOMPtr<nsISupportsString> wideString;
        wideString = do_QueryInterface(item);
        if (!wideString)
            return;

        nsAutoString ucs2string;
        wideString->GetData(ucs2string);
        NS_ConvertUTF16toUTF8 utf8string(ucs2string);

        gtk_selection_data_set_text(aSelectionData, utf8string.get(),
                                    utf8string.Length());
        return;
    }

    // Check to see if the selection data is an image type
    if (gtk_targets_include_image(&selectionTarget, 1, TRUE)) {
        // Look through our transfer data for the image
        static const char* const imageMimeTypes[] = {
            kNativeImageMime, kPNGImageMime, kJPEGImageMime, kJPGImageMime, kGIFImageMime };
        nsCOMPtr<nsISupports> imageItem;
        nsCOMPtr<imgIContainer> image;
        for (uint32_t i = 0; i < ArrayLength(imageMimeTypes); i++) {
            rv = trans->GetTransferData(imageMimeTypes[i], getter_AddRefs(imageItem));
            if (NS_FAILED(rv)) {
              continue;
            }

            image = do_QueryInterface(imageItem);
            if (image) {
                break;
            }
        }

        if (!image) { // Not getting an image for an image mime type!?
            return;
        }

        GdkPixbuf* pixbuf = nsImageToPixbuf::ImageToPixbuf(image);
        if (!pixbuf)
            return;

        gtk_selection_data_set_pixbuf(aSelectionData, pixbuf);
        g_object_unref(pixbuf);
        return;
    }

    if (selectionTarget == gdk_atom_intern(kHTMLMime, FALSE)) {
        rv = trans->GetTransferData(kHTMLMime, getter_AddRefs(item));
        if (!item || NS_FAILED(rv)) {
            return;
        }

        nsCOMPtr<nsISupportsString> wideString;
        wideString = do_QueryInterface(item);
        if (!wideString) {
            return;
        }

        nsAutoString ucs2string;
        wideString->GetData(ucs2string);

        nsAutoCString html;
        // Add the prefix so the encoding is correctly detected.
        html.AppendLiteral(kHTMLMarkupPrefix);
        AppendUTF16toUTF8(ucs2string, html);

        gtk_selection_data_set(aSelectionData, selectionTarget, 8,
                               (const guchar*)html.get(), html.Length());
        return;
    }

    // Try to match up the selection data target to something our
    // transferable provides.
    gchar *target_name = gdk_atom_name(selectionTarget);
    if (!target_name)
        return;

    rv = trans->GetTransferData(target_name, getter_AddRefs(item));
    // nothing found?
    if (!item || NS_FAILED(rv)) {
        g_free(target_name);
        return;
    }

    void *primitive_data = nullptr;
    uint32_t dataLen = 0;
    nsPrimitiveHelpers::CreateDataFromPrimitive(nsDependentCString(target_name),
                                                item, &primitive_data, &dataLen);

    if (primitive_data) {
        gtk_selection_data_set(aSelectionData, selectionTarget,
                               8, /* 8 bits in a unit */
                               (const guchar *)primitive_data, dataLen);
        free(primitive_data);
    }

    g_free(target_name);

}

void
nsClipboard::SelectionClearEvent(GtkClipboard *aGtkClipboard)
{
    int32_t whichClipboard;

    // which clipboard?
    if (aGtkClipboard == gtk_clipboard_get(GDK_SELECTION_PRIMARY))
        whichClipboard = kSelectionClipboard;
    else if (aGtkClipboard == gtk_clipboard_get(GDK_SELECTION_CLIPBOARD))
        whichClipboard = kGlobalClipboard;
    else
        return; // THAT AIN'T NO CLIPBOARD I EVER HEARD OF

    EmptyClipboard(whichClipboard);
}

void
clipboard_get_cb(GtkClipboard *aGtkClipboard,
                 GtkSelectionData *aSelectionData,
                 guint info,
                 gpointer user_data)
{
    nsClipboard *aClipboard = static_cast<nsClipboard *>(user_data);
    aClipboard->SelectionGetEvent(aGtkClipboard, aSelectionData);
}

void
clipboard_clear_cb(GtkClipboard *aGtkClipboard,
                   gpointer user_data)
{
    nsClipboard *aClipboard = static_cast<nsClipboard *>(user_data);
    aClipboard->SelectionClearEvent(aGtkClipboard);
}

/*
 * when copy-paste, mozilla wants data encoded using UCS2,
 * other app such as StarOffice use "text/html"(RFC2854).
 * This function convert data(got from GTK clipboard)
 * to data mozilla wanted.
 *
 * data from GTK clipboard can be 3 forms:
 *  1. From current mozilla
 *     "text/html", charset = utf-16
 *  2. From old version mozilla or mozilla-based app
 *     content("body" only), charset = utf-16
 *  3. From other app who use "text/html" when copy-paste
 *     "text/html", has "charset" info
 *
 * data      : got from GTK clipboard
 * dataLength: got from GTK clipboard
 * body      : pass to Mozilla
 * bodyLength: pass to Mozilla
 */
void ConvertHTMLtoUCS2(const char* data, int32_t dataLength,
                       char16_t** unicodeData, int32_t& outUnicodeLen)
{
    nsAutoCString charset;
    GetHTMLCharset(data, dataLength, charset);// get charset of HTML
    if (charset.EqualsLiteral("UTF-16")) {//current mozilla
        outUnicodeLen = (dataLength / 2) - 1;
        *unicodeData =
            reinterpret_cast<char16_t*>
            (moz_xmalloc((outUnicodeLen + sizeof('\0')) * sizeof(char16_t)));
        memcpy(*unicodeData, data + sizeof(char16_t),
               outUnicodeLen * sizeof(char16_t));
        (*unicodeData)[outUnicodeLen] = '\0';
    } else if (charset.EqualsLiteral("UNKNOWN")) {
        outUnicodeLen = 0;
        return;
    } else {
        // app which use "text/html" to copy&paste
        // get the decoder
        auto encoding = Encoding::ForLabelNoReplacement(charset);
        if (!encoding) {
#ifdef DEBUG_CLIPBOARD
            g_print("        get unicode decoder error\n");
#endif
            outUnicodeLen = 0;
            return;
        }

        auto dataSpan = MakeSpan(data, dataLength);
        // Remove kHTMLMarkupPrefix again, it won't necessarily cause any
        // issues, but might confuse other users.
        const size_t prefixLen = ArrayLength(kHTMLMarkupPrefix) - 1;
        if (dataSpan.Length() >= prefixLen &&
            Substring(data, prefixLen).EqualsLiteral(kHTMLMarkupPrefix)) {
          dataSpan = dataSpan.From(prefixLen);
        }

        auto decoder = encoding->NewDecoder();
        CheckedInt<size_t> needed =
            decoder->MaxUTF16BufferLength(dataSpan.Length());
        if (!needed.isValid() || needed.value() > INT32_MAX) {
          outUnicodeLen = 0;
          return;
        }

        outUnicodeLen = 0;
        if (needed.value()) {
          *unicodeData = reinterpret_cast<char16_t*>(
            moz_xmalloc((needed.value() + 1) * sizeof(char16_t)));
          uint32_t result;
          size_t read;
          size_t written;
          bool hadErrors;
          Tie(result, read, written, hadErrors) =
            decoder->DecodeToUTF16(AsBytes(dataSpan),
                                   MakeSpan(*unicodeData, needed.value()),
                                   true);
          MOZ_ASSERT(result == kInputEmpty);
          MOZ_ASSERT(read == size_t(dataSpan.Length()));
          MOZ_ASSERT(written <= needed.value());
          Unused << hadErrors;
          outUnicodeLen = written;
          // null terminate.
          (*unicodeData)[outUnicodeLen] = '\0';
        } // if valid length
    }
}

/*
 * get "charset" information from clipboard data
 * return value can be:
 *  1. "UTF-16":      mozilla or "text/html" with "charset=utf-16"
 *  2. "UNKNOWN":     mozilla can't detect what encode it use
 *  3. other:         "text/html" with other charset than utf-16
 */
void GetHTMLCharset(const char* data, int32_t dataLength, nsCString& str)
{
    // if detect "FFFE" or "FEFF", assume UTF-16
    char16_t* beginChar =  (char16_t*)data;
    if ((beginChar[0] == 0xFFFE) || (beginChar[0] == 0xFEFF)) {
        str.AssignLiteral("UTF-16");
        return;
    }
    // no "FFFE" and "FEFF", assume ASCII first to find "charset" info
    const nsDependentCSubstring htmlStr(data, dataLength);
    nsACString::const_iterator start, end;
    htmlStr.BeginReading(start);
    htmlStr.EndReading(end);
    nsACString::const_iterator valueStart(start), valueEnd(start);

    if (CaseInsensitiveFindInReadable(
        NS_LITERAL_CSTRING("CONTENT=\"text/html;"),
        start, end)) {
        start = end;
        htmlStr.EndReading(end);

        if (CaseInsensitiveFindInReadable(
            NS_LITERAL_CSTRING("charset="),
            start, end)) {
            valueStart = end;
            start = end;
            htmlStr.EndReading(end);

            if (FindCharInReadable('"', start, end))
                valueEnd = start;
        }
    }
    // find "charset" in HTML
    if (valueStart != valueEnd) {
        str = Substring(valueStart, valueEnd);
        ToUpperCase(str);
#ifdef DEBUG_CLIPBOARD
        printf("Charset of HTML = %s\n", charsetUpperStr.get());
#endif
        return;
    }
    str.AssignLiteral("UNKNOWN");
}
