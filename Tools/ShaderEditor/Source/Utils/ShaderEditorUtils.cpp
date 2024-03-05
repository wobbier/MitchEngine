#include "ShaderEditorUtils.h"

#if USING( ME_PLATFORM_WIN64 )
#include <Windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shobjidl.h>
#endif

#if USING( ME_PLATFORM_WIN64 )
int CALLBACK BrowseForFolderCallback( HWND hwnd, UINT uMsg, LPARAM lp, LPARAM pData )
{
    //char szPath[MAX_PATH];

    switch( uMsg )
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
    if( SUCCEEDED( hr ) ) {
        // Set options for the Common Item Dialog
        DWORD dwOptions;
        pFileOpenDialog->GetOptions( &dwOptions );
        pFileOpenDialog->SetOptions( dwOptions | FOS_FORCEFILESYSTEM );

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
        if( SUCCEEDED( hr ) ) {
            // Get the selected file path
            IShellItem* pItem;
            hr = pFileOpenDialog->GetResult( &pItem );
            if( SUCCEEDED( hr ) ) {
                PWSTR pszFilePath;
                hr = pItem->GetDisplayName( SIGDN_FILESYSPATH, &pszFilePath );
                if( SUCCEEDED( hr ) ) {
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
#endif