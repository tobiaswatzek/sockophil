//
// Created by tobias on 02/10/16.
//

#include <string>
#include <nlohmann/json.hpp>
#include "sockclient/NoFilenameException.h"
#include "sockclient/ClientSelection.h"

namespace sockclient {
    /**
     * Constructor that sets the member vars
     * @param action is the action that should be executed
     * @param filename is either the filename of the file to download or upload or empty (quit, list)
     */
    ClientSelection::ClientSelection(sockophil::client_action action, std::string filename) :
            action(action),
            filename(filename) {
        if(filename.empty() && ((action == sockophil::put) || (action == sockophil::get))) {
            throw NoFilenameException();
        }

    }
    /**
     * Consturctor that sets the member vars by calling the main constructor
     * @param action is the action that should be executed
     */
    ClientSelection::ClientSelection(sockophil::client_action action) : ClientSelection(action, "") {}

    /**
     * Get the action
     * @return action
     */
    sockophil::client_action ClientSelection::get_action() const noexcept {
        return this->action;
    }

    /**
     * Get the filename
     * @return filename
     */
    std::string ClientSelection::get_filename() const noexcept {
        return this->filename;
    }

    nlohmann::json ClientSelection::to_json() const {
        nlohmann::json jsn = {
                {"action", this->get_action()},
                {"filename", this->get_filename()}
        };
        return jsn;
    }
}