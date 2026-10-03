# Before you publish

None of the items below has been checked for you. The engine is tested automatically, but the plug-in has been built against the SDK and has not been through a full test in After Effects, so do the technical part before you put it on sale.

## Technical checks, in After Effects 2026

1. Run `package.bat` and check that the zip contains Timeyum.aex (over 1.1 MB, because the banner picture is inside it), INSTALL.txt, USER_GUIDE.pdf, LICENSE.txt and CHANGELOG.txt.
2. In a command prompt run `dumpbin /dependents build-release\Release\Timeyum.aex`. It should list only Windows system DLLs. If it lists VCRUNTIME140.dll or MSVCP140.dll, customers need the Visual C++ runtime and the build must be fixed first.
3. Install it the way a customer would, on a second PC if you can. The effect must appear under Effects, Timeyum, and the picture must show at the top of the panel. If it does not, read `%TEMP%\timeyum_log.txt`.
4. Apply it to a normal layer and to an adjustment layer. Try 8, 16 and 32 bits per channel, a layer with alpha, a 4K comp. Try layers that are not the size of the composition: smaller, larger, moved so that part of the layer is outside the frame, scaled, and a precomp. Copy the effect from one layer and paste it onto another, then apply a fresh one to a third. Each must render the same picture.
5. Switch the composition preview to half and third resolution. The preview must look like the full resolution render, and the Pull Point must stay on the same spot of the image.
6. Check that the Pull Point starts at the centre of the layer. Drag it in the composition panel. Link it to a null with an expression.
7. Render with Multi-Frame Rendering on, and again with it off. The frames must be identical. Render the same frame twice, in different orders: identical again.
8. Save the project, close it, reopen it: all values must persist. Duplicate the layer, undo, redo.
9. In the Warp group, test on a clip with something moving: raise Motion Response and the streaks should react from the second frame onward. Look at View, Reaction, to see what the effect sees.
10. Control input: choose a control layer (a solid with a mask, a precomp, an alpha matte, a grayscale depth map, a 32 bit depth pass) with each of the six channels and with This Layer Alpha and Luminance. Turn on Show Control and check that it matches what you expect. Try Effect Matte, Streak Source Matte, Streak Length from Control and Warp Drive from Control one at a time. Use a control layer of another size, and check that a Control Layer set to None does nothing.
11. Click through the panel changing values, the popups and the checkboxes: every control must keep its look, nothing must switch between a number and a slider by itself.
12. Open the About box and check the version number.
13. The effect's match name, `Timeyum Timeshift`, must never change after you release: saved projects refer to it.

## Business and legal

1. The licence in LICENSE is a plain agreement written for this project. Have a lawyer read it before you rely on it.
2. The picture on the cover and in the panel is a frame of film footage. Confirm that you own the footage or have the right to use it commercially. If not, replace ae/TimeyumBanner.webp and regenerate the covers.
3. Add your support email to the receipt message and to the FAQ.
4. If the source repository is public, anyone can build the plug-in from it. Decide whether to make the repository private before you start selling; the licence forbids redistribution but does not stop a download.
5. The plug-in is not code-signed, so Windows may show a warning on download. A code-signing certificate removes it; INSTALL.txt already tells customers to unblock the zip.
6. The panel and the About box link to https://github.com/ramirstudio/Timeyum. Change the URL in `ae/TimeyumAE.cpp` and `ae/TimeyumPiPL.r` if you prefer to point at your Gumroad page.
7. Check Gumroad's current fees, tax handling and refund options in your account, and set the refund policy to what you will honour.

## Media to capture (they need your own footage)

- A before and after on the same shot, with a clear highlight against a dark area.
- The Camera model with Timing Shift moving from 0 to 100 degrees.
- Warp with the pull point dragged across the frame, and the Reaction view next to the result.
- A screenshot of the full Timeyum panel with the banner at the top.
- A 30 to 60 second video that puts the first three together, uploaded to Gumroad or linked from YouTube or Vimeo.

## Publishing

1. Create the product on Gumroad and paste the fields from LISTING.md.
2. Upload Timeyum-1.0.0-win64.zip as the product file.
3. Buy it yourself with a 100% discount code and install the downloaded copy on a clean machine.
4. Publish.
