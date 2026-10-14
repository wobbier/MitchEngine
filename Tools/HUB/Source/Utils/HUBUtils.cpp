#include "HUBUtils.h"

#if USING( ME_PLATFORM_WIN64 )
#include <Windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shobjidl.h>
#elif USING( ME_PLATFORM_LINUX )
#include <gtk/gtk.h>
#endif

#if USING( ME_PLATFORM_WIN64 )
int CALLBACK BrowseForFolderCallback( HWND hwnd, UINT uMsg, LPARAM lp, LPARAM pData )
{
    //char szPath[MAX_PATH];

    switch ( uMsg )
    {
    case BFFM_INITIALIZED:
        //SendMessage(hwnd, BFFM_SETSELECTION, TRUE, pData);
        break;

    case BFFM_SELCHANGED:
        /*if (SHGetPathFromIDList((LPITEMIDLIST)lp, szPath))
        {
            SendMessage(hwnd, BFFM_SETSTATUSTEXT, 0, (LPARAM)szPath);

        }*/
        break;
    }

    return 0;
}

Path HUB::ShowOpenFilePrompt( void* hwnd )
{
    Path foundPath;
    // Initialize COM
    CoInitialize( NULL );

    // Create an instance of the Common Item Dialog
    IFileOpenDialog* pFileOpenDialog;
    HRESULT hr = CoCreateInstance( CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS( &pFileOpenDialog ) );
    if ( SUCCEEDED( hr ) ) {
        // Set options for the Common Item Dialog
        DWORD dwOptions;
        pFileOpenDialog->GetOptions( &dwOptions );
        pFileOpenDialog->SetOptions( dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM );

        // Restrict the dialog to show only certain file types
        COMDLG_FILTERSPEC fileTypes[] = {
            { L"OBJ Files", L"*.obj" },
            { L"PNG Files", L"*.png" },
            { L"BIN Files", L"*.bin" }
        };
        pFileOpenDialog->SetFileTypes( _countof( fileTypes ), fileTypes );

        // Set the title of the dialog box
        pFileOpenDialog->SetTitle( L"Select a File" );

        // Display the Common Item Dialog
        hr = pFileOpenDialog->Show( NULL );
        if ( SUCCEEDED( hr ) ) {
            // Get the selected file path
            IShellItem* pItem;
            hr = pFileOpenDialog->GetResult( &pItem );
            if ( SUCCEEDED( hr ) ) {
                PWSTR pszFilePath;
                hr = pItem->GetDisplayName( SIGDN_FILESYSPATH, &pszFilePath );
                if ( SUCCEEDED( hr ) ) {
                    // Convert the path to a UTF-8 string
                    std::wstring widePath( pszFilePath );
                    std::string path( widePath.begin(), widePath.end() );
                    foundPath = Path( path );

                    CoTaskMemFree( pszFilePath );
                }
                pItem->Release();
            }
        }
        pFileOpenDialog->Release();
    }

    // Uninitialize COM
    CoUninitialize();
    return foundPath;
}
#elif USING( ME_PLATFORM_LINUX )

Path HUB::ShowOpenFilePrompt( void* hwnd )
{
    // The SDL native handle is not a GtkWindow, so this dialog has no GTK parent.
    (void)hwnd;
    if( !gtk_init_check( nullptr, nullptr ) )
    {
        CLog::Log( CLog::LogType::Error, "Could not initialize GTK for the project folder picker." );
        return {};
    }

    GtkFileChooserNative* dialog = gtk_file_chooser_native_new(
        "Select a Project Folder", nullptr, GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "Select", "Cancel" );
    gtk_file_chooser_set_local_only( GTK_FILE_CHOOSER( dialog ), TRUE );

    Path selectedPath;
    if( gtk_native_dialog_run( GTK_NATIVE_DIALOG( dialog ) ) == GTK_RESPONSE_ACCEPT )
    {
        char* folder = gtk_file_chooser_get_filename( GTK_FILE_CHOOSER( dialog ) );
        if( folder )
        {
            Path candidate( folder );
            if( candidate.Exists && candidate.IsFolder )
            {
                selectedPath = candidate;
            }
            g_free( folder );
        }
    }

    g_object_unref( dialog );
    return selectedPath;
}
#endif
